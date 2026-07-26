#include "wordclock/Health.h"

namespace wordclock {

HealthState evaluate(const HealthInputs& in) {
    HealthState s;

    // Das Wortfeld haengt allein daran, ob es je eine echte Zeit gab. Eine
    // veraltete Zeit wird weiter angezeigt -- bei 5 Minuten Anzeigeschritt ist
    // sie tagelang korrekt -- eine nie gestellte Uhr waere reine Erfindung.
    s.wordFieldDark = !in.everSynced;

    const bool timeStale = in.everSynced && in.secondsSinceSync >= kSyncWarnSeconds &&
                           in.secondsSinceSync < kSyncCriticalSeconds;
    const bool timeUntrusted = !in.everSynced || in.secondsSinceSync >= kSyncCriticalSeconds;

    // Reihenfolge ist Absicht: zuerst was alles andere unbrauchbar macht, dann
    // was den Benutzer braucht, dann die Ursache vor der Folge (ohne WLAN kein
    // NTP), zuletzt die reinen Hinweise.
    if (!in.configOk) {
        s.severity = Severity::Critical;
        s.fault = Fault::ConfigBroken;
        s.rhythm = Rhythm::Blinking;
        s.code = 3;
    } else if (in.apActive) {
        s.severity = Severity::Critical;
        s.fault = Fault::ApMode;
        s.rhythm = Rhythm::Chase;  // Lauflicht: wartet auf dich
        s.code = 4;
    } else if (!in.wifiConnected) {
        s.severity = Severity::Critical;
        s.fault = Fault::NoWifi;
        s.rhythm = Rhythm::Blinking;
        s.code = 1;
    } else if (timeUntrusted) {
        s.severity = Severity::Critical;
        s.fault = Fault::NoTime;
        s.rhythm = Rhythm::Blinking;
        s.code = 2;
    } else if (timeStale) {
        s.severity = Severity::Warning;
        s.fault = Fault::TimeStale;
        s.rhythm = Rhythm::Breathing;
    } else if (!in.mqttConnected) {
        s.severity = Severity::Warning;
        s.fault = Fault::MqttDown;
        s.rhythm = Rhythm::Breathing;
    }

    return s;
}

const char* faultName(Fault f) {
    switch (f) {
        case Fault::None: return "ok";
        case Fault::MqttDown: return "mqtt_down";
        case Fault::TimeStale: return "time_stale";
        case Fault::NoWifi: return "no_wifi";
        case Fault::NoTime: return "no_time";
        case Fault::ConfigBroken: return "config_broken";
        case Fault::ApMode: return "ap_mode";
    }
    return "unknown";
}

}  // namespace wordclock
