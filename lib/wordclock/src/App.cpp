#include "wordclock/App.h"

namespace wordclock {

void App::begin() {
    if (ports_.storage) ports_.storage->load(config_);
    applyStyles(DisplayState::Day);

    mqtt_.begin(ports_.mqtt, &config_, &notify_, ports_.system);

    lastChangeRevision_ = config_.revision();
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

    Frame base;
    base.clear();

    // Ohne je gestellte Zeit bleibt das Wortfeld dunkel -- lieber nichts als
    // etwas Erfundenes (DESIGN 5).
    if (!health.wordFieldDark && !panelOff)
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
