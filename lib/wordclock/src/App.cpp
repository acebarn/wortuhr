#include "wordclock/App.h"

#include <cstring>

namespace wordclock {

void App::begin(const Secrets* seed) {
    if (ports_.storage) {
        ports_.storage->load(config_);
        ports_.storage->loadSecrets(secrets_);
    }

    // Nur befuellen, was noch leer ist. Was einmal ueber die Webapp gesetzt
    // wurde, darf ein Neuflashen nicht zurueckdrehen.
    if (seed) {
        for (uint8_t i = 0; i < kSecretCount; ++i) {
            const SecretKey k = SecretKey(i);
            if (!secrets_.isSet(k) && seed->isSet(k)) secrets_.set(k, seed->get(k));
        }
    }

    applyStyles(DisplayState::Day);
    webApi_.begin(&config_, &secrets_);
    mqtt_.begin(ports_.mqtt, &config_, &notify_, ports_.system);

    if (ports_.network)
        ports_.network->applyCredentials(secrets_.get(SecretKey::WifiSsid),
                                         secrets_.get(SecretKey::WifiPass));
    if (ports_.mqtt)
        ports_.mqtt->applyBroker(secrets_.get(SecretKey::MqttHost), secrets_.mqttPort(),
                                 secrets_.get(SecretKey::MqttUser),
                                 secrets_.get(SecretKey::MqttPass));

    lastChangeRevision_ = config_.revision();
    lastSecretRevision_ = secrets_.revision();
}

void App::refreshWebStatus() {
    WebStatus st;
    st.health = snapshot_.health;
    st.hours = snapshot_.hours;
    st.minutes = snapshot_.minutes;
    st.displayState = snapshot_.state;
    st.wifiConnected = ports_.network ? ports_.network->connected() : false;
    st.apActive = ports_.network ? ports_.network->apActive() : false;
    st.rssi = ports_.network ? ports_.network->rssi() : 0;
    st.mqttEnabled = mqtt_.enabled();
    st.mqttConnected = mqtt_.connected();
    st.heap = ports_.system ? ports_.system->freeHeap() : 0;
    st.uptimeS = ports_.clock ? ports_.clock->nowMs() / 1000 : 0;
    if (ports_.network) st.ip = ports_.network->ip();
    webApi_.setStatus(st);
}

void App::applyWebAction(WebAction action) {
    switch (action) {
        case WebAction::None:
            break;

        case WebAction::ReconnectWifi:
            if (ports_.storage) ports_.storage->saveSecrets(secrets_);
            if (ports_.network)
                ports_.network->applyCredentials(secrets_.get(SecretKey::WifiSsid),
                                                 secrets_.get(SecretKey::WifiPass));
            break;

        case WebAction::ReconnectMqtt:
            if (ports_.storage) ports_.storage->saveSecrets(secrets_);
            if (ports_.mqtt)
                ports_.mqtt->applyBroker(secrets_.get(SecretKey::MqttHost), secrets_.mqttPort(),
                                         secrets_.get(SecretKey::MqttUser),
                                         secrets_.get(SecretKey::MqttPass));
            break;

        case WebAction::FactoryReset:
            config_.reset();
            secrets_.reset();
            if (ports_.storage) {
                ports_.storage->save(config_);
                ports_.storage->saveSecrets(secrets_);
            }
            if (ports_.system) ports_.system->restart();
            break;

        case WebAction::PlayAnimation: {
            // Ueber den Notify-Stapel, nicht am Animator vorbei: so gelten
            // dieselben Regeln wie fuer HomeAssistant -- Prioritaet, Ablauf,
            // Selbstheilung, und im Nacht- und Aus-Zustand bleibt es aus.
            NotifyRequest req;
            req.id = "webtest";
            req.prio = 120;
            req.style = NotifyStyle::Anim;
            req.anim = webApi_.pendingAnimation();
            req.ttlSeconds = webApi_.pendingAnimationSeconds();
            notify_.push(req, ports_.clock ? ports_.clock->nowMs() : 0);
            break;
        }

        case WebAction::StopAnimation:
            notify_.clear("webtest");
            break;

        case WebAction::Restart:
            // Ausstehende Aenderungen nicht verlieren.
            if (ports_.storage && config_.dirty()) ports_.storage->save(config_);
            if (ports_.system) ports_.system->restart();
            break;
    }
}

void App::applyStyles(DisplayState state) {
    clockRenderer_.setStyle(clockStyleFor(config_, state));
    dotRenderer_.setStyle(dotStyleFor(config_, state));
    compositor_.setSmoothing(config_.getU8(ConfigKey::Smoothing));
    compositor_.setCurrentLimit(config_.getU16(ConfigKey::CurrentLimit));
    appliedState_ = state;
    appliedRevision_ = config_.revision();
    stylesApplied_ = true;
}

HealthInputs App::gatherHealth() {
    HealthInputs in;
    in.configOk = ports_.storage ? ports_.storage->ok() : true;
    in.wifiConnected = ports_.network ? ports_.network->connected() : false;
    in.apActive = ports_.network ? ports_.network->apActive() : false;
    in.mqttEnabled = mqtt_.enabled();
    in.mqttConnected = mqtt_.connected();
    in.everSynced = ports_.clock ? ports_.clock->everSynced() : false;
    in.secondsSinceSync = ports_.clock ? ports_.clock->secondsSinceSync() : 0;
    return in;
}

void App::autosave(uint32_t nowMs) {
    if (!ports_.storage) return;

    // Zugangsdaten sofort sichern, nicht entprellt: sie werden selten und
    // bewusst geaendert, und ein Verlust waere teuer. Zuerst geprueft, damit
    // eine gleichzeitige Konfigurationsaenderung sie nicht ueberspringt.
    if (secrets_.revision() != lastSecretRevision_) {
        lastSecretRevision_ = secrets_.revision();
        if (secrets_.dirty()) ports_.storage->saveSecrets(secrets_);
    }

    const uint32_t rev = config_.revision();
    if (rev != lastChangeRevision_) {  // es hat sich gerade wieder etwas geruehrt
        lastChangeRevision_ = rev;
        lastChangeMs_ = nowMs;
        return;
    }
    if (config_.dirty() && (nowMs - lastChangeMs_) >= kAutosaveQuietMs)
        ports_.storage->save(config_);
}

void App::tick() {
    const uint32_t nowMs = ports_.clock ? ports_.clock->nowMs() : 0;

    mqtt_.tick(nowMs);
    notify_.tick(nowMs, mqtt_.connected());
    autosave(nowMs);

    if (nowMs - lastFrameMs_ < frameIntervalMs_) return;
    lastFrameMs_ = nowMs;

    const HealthState health = evaluate(gatherHealth());

    uint8_t hours = 0, minutes = 0;
    if (!health.wordFieldDark && ports_.clock) ports_.clock->localHm(hours, minutes);

    // Ohne gueltige Zeit gibt es keine Zeitfenster -- dann gilt Tag. Sonst
    // koennte die Uhr im Aus-Zustand haengen bleiben und nie wieder erscheinen.
    const DisplayState state =
        health.wordFieldDark ? DisplayState::Day : displayStateFor(config_, hours, minutes);
    const bool panelOff = (state == DisplayState::Off);

    if (!stylesApplied_ || state != appliedState_ || config_.revision() != appliedRevision_)
        applyStyles(state);

    // --- Animationen ------------------------------------------------------
    //
    // Sie ersetzen die Basis-Ebene, weil 110 Pixel gleichzeitig die Buchstaben
    // sind. Im Nacht- und Aus-Zustand unterbleiben sie (DESIGN 7.2).
    const bool mayAnimate = (state == DisplayState::Day) && !health.wordFieldDark;

    if (mayAnimate && config_.getBool(ConfigKey::ChimeEnabled) && minutes == 0 &&
        hours != lastChimeHour_) {
        lastChimeHour_ = hours;
        const uint8_t idx = config_.getU8(ConfigKey::ChimeStyle);
        if (animator_.startByName(kChimeOptions[idx < 8 ? idx : 0], nowMs))
            chimeUntilMs_ = nowMs + uint32_t(config_.getU16(ConfigKey::ChimeSeconds)) * 1000;
    }
    if (minutes != 0) lastChimeHour_ = 0xFF;

    // Von HomeAssistant ausgeloest -- laeuft, solange der Kanal oben liegt.
    const char* wanted = notify_.activeAnimation();
    if (wanted && mayAnimate) {
        if (!animator_.running() || std::strcmp(lastAnimName_, wanted) != 0) {
            if (animator_.startByName(wanted, nowMs)) {
                std::strncpy(lastAnimName_, wanted, sizeof(lastAnimName_) - 1);
                lastAnimName_[sizeof(lastAnimName_) - 1] = '\0';
                chimeUntilMs_ = 0;  // laeuft ohne Frist
            }
        }
    } else if (chimeUntilMs_ && int32_t(nowMs - chimeUntilMs_) >= 0) {
        animator_.stop();
        chimeUntilMs_ = 0;
    } else if (!wanted && !chimeUntilMs_) {
        animator_.stop();
        lastAnimName_[0] = '\0';
    }
    if (!mayAnimate) {
        animator_.stop();
        chimeUntilMs_ = 0;
    }

    Frame base;
    base.clear();

    // Ohne je gestellte Zeit bleibt das Wortfeld dunkel -- lieber nichts als
    // etwas Erfundenes (DESIGN 5).
    if (animator_.running())
        animator_.render(base, nowMs);
    else if (!health.wordFieldDark && !panelOff)
        clockRenderer_.render(base, hours, minutes, nowMs);

    dotRenderer_.render(base, health, minutes, nowMs, panelOff);

    Modifiers mod;
    mod.brightness = brightnessFor(config_, state);
    overlay_.clear();
    notify_.apply(mod, overlay_, nowMs, clockRenderer_.style().breathDepth > 0);

    const Frame& out = compositor_.step(base, overlay_, mod);
    if (ports_.strip) ports_.strip->show(out);

    snapshot_.hours = hours;
    snapshot_.minutes = minutes;
    snapshot_.state = state;
    snapshot_.health = health;
    snapshot_.currentMa = estimateCurrentMa(out);
    snapshot_.animating = animator_.running();
    ++snapshot_.frames;

    if (nowMs - lastDiagMs_ >= kDiagIntervalMs) {
        lastDiagMs_ = nowMs;
        MqttService::Diagnostics d;
        d.status = faultName(health.fault);
        d.rssi = ports_.network ? ports_.network->rssi() : 0;
        d.heap = ports_.system ? ports_.system->freeHeap() : 0;
        d.uptimeS = nowMs / 1000;
        d.syncAgeS = ports_.clock ? ports_.clock->secondsSinceSync() : 0;
        d.currentMa = snapshot_.currentMa;
        mqtt_.publishDiagnostics(nowMs, d);
    }
}

}  // namespace wordclock
