#pragma once

#include <cstdint>

namespace wordclock {

// Bewertung des Geraetezustands. Reine Politik, keine Darstellung -- wie ein
// Zustand aussieht, entscheidet DotRenderer.
//
// Grundlage ist DESIGN 4 und 5. Der Vertrag lautet: Ruhe bedeutet gesund, jede
// Bewegung bedeutet Stoerung. Die Uhrenfarbe ist frei konfigurierbar, deshalb
// traegt der Rhythmus das Signal und nicht die Farbe.

// Schwellen fuer das Zeitvertrauen, hergeleitet aus dem Anzeigeschritt: die
// Anzeige aendert sich alle 5 Minuten, ein sichtbarer Fehler entsteht also erst
// bei rund 150 s Abweichung. Der ESP8266 driftet frei laufend in der
// Groessenordnung Sekunden pro Tag.
inline constexpr uint32_t kSyncWarnSeconds = 3UL * 24 * 3600;       // 3 Tage
inline constexpr uint32_t kSyncCriticalSeconds = 14UL * 24 * 3600;  // 14 Tage

// Was die Adapter beobachten.
struct HealthInputs {
    bool configOk = true;        // Dateisystem und Konfiguration lesbar
    bool wifiConnected = false;  // Verbindung ins Heimnetz steht
    bool apActive = false;       // eigener Accesspoint ist offen
    bool mqttConnected = false;  // Broker erreichbar
    bool everSynced = false;     // jemals eine echte Uhrzeit gehabt
    uint32_t secondsSinceSync = 0;  // nur sinnvoll, wenn everSynced
};

enum class Severity : uint8_t {
    Ok,
    Warning,   // Uhr laeuft korrekt weiter, Anzahl bleibt die Minute
    Critical,  // Anzahl und Farbe werden zum Fehlercode
};

enum class Fault : uint8_t {
    None,
    MqttDown,      // Warnung -- Uhr laeuft, nur HomeAssistant fehlt
    TimeStale,     // Warnung -- Sync 3 bis 14 Tage her
    NoWifi,        // kritisch, Code 1
    NoTime,        // kritisch, Code 2 -- nie synchronisiert oder ueber 14 Tage
    ConfigBroken,  // kritisch, Code 3
    ApMode,        // kritisch, Code 4 -- wartet auf Einrichtung
};

enum class Rhythm : uint8_t {
    Steady,     // gesund
    Breathing,  // Warnung
    Blinking,   // kritisch
    Chase,      // wartet auf den Benutzer
};

struct HealthState {
    Severity severity = Severity::Ok;
    Fault fault = Fault::None;
    Rhythm rhythm = Rhythm::Steady;

    // Anzahl der Punkte bei kritischem Zustand (1..4). Im gesunden Zustand und
    // bei Warnungen 0 -- dort zaehlt die Minute.
    uint8_t code = 0;

    // Es gab nie eine echte Uhrzeit. Dann bleibt das Wortfeld dunkel: die Uhr
    // zeigt lieber nichts als etwas Erfundenes (DESIGN 5).
    bool wordFieldDark = false;

    bool operator==(const HealthState& o) const {
        return severity == o.severity && fault == o.fault && rhythm == o.rhythm &&
               code == o.code && wordFieldDark == o.wordFieldDark;
    }
    bool operator!=(const HealthState& o) const { return !(*this == o); }
};

// Hoechstrangige zutreffende Stoerung bestimmen.
HealthState evaluate(const HealthInputs& in);

// Kurzform fuer Logausgaben und die Diagnose-Entitaet in HomeAssistant.
const char* faultName(Fault f);

}  // namespace wordclock
