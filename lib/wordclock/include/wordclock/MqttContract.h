#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "wordclock/Config.h"

namespace wordclock {

// Der MQTT-Vertrag: Topics, Wertdarstellung und HomeAssistant-Discovery.
//
// Reine Logik, ohne Netzwerk -- damit die Telegramme auf dem Rechner geprueft
// werden koennen statt erst im HA-Log.
//
// HOHEIT (DESIGN 8.1): Die Uhr ist alleinige Wahrheit.
//   - Command-Topics NIE retained. Sonst liefert der Broker beim naechsten
//     Start Befehle aus, die Wochen alt sind.
//   - State-Topic IMMER retained, damit HA nach einem Neustart sofort den
//     richtigen Wert zeigt.
//   - Alle Entitaeten bekommen ein state_topic. Damit arbeitet HA
//     nicht-optimistisch: es zeigt nur, was die Uhr bestaetigt hat.

inline constexpr char kDefaultPrefix[] = "wortuhr";
inline constexpr char kDiscoveryPrefix[] = "homeassistant";
inline constexpr char kPayloadOnline[] = "online";
inline constexpr char kPayloadOffline[] = "offline";

// --- Topics ----------------------------------------------------------------

void topicAvailability(char* out, size_t n, const char* prefix);
void topicState(char* out, size_t n, const char* prefix);
void topicDiag(char* out, size_t n, const char* prefix);
void topicNotify(char* out, size_t n, const char* prefix);
void topicSet(char* out, size_t n, const char* prefix, const char* key);
void topicSetWildcard(char* out, size_t n, const char* prefix);

// homeassistant/<component>/<device>/<key>/config
void topicDiscovery(char* out, size_t n, const char* component, const char* device,
                    const char* key);

// Aus "<prefix>/set/<key>" den Schluessel herausloesen. false, wenn das Topic
// nicht dazu passt.
bool keyFromSetTopic(const char* topic, const char* prefix, char* out, size_t n);

// --- Wertdarstellung -------------------------------------------------------
//
// HomeAssistant sendet und erwartet Text. Zahlen gehen direkt, aber Farben
// wollen "RRGGBB" und Uhrzeiten "HH:MM" -- als rohe Minutenzahl waere eine
// Nachtzeit nicht bedienbar.

void formatValue(const ConfigItem& item, int32_t value, char* out, size_t n);
bool parseValue(const ConfigItem& item, const char* text, int32_t& out);

// --- HomeAssistant-Discovery ------------------------------------------------

// Entitaetstyp fuer einen Schema-Eintrag.
const char* componentFor(ConfigType t);

struct DeviceInfo {
    const char* id = kDefaultPrefix;
    const char* name = "Wortuhr";
    const char* manufacturer = "Eigenbau";
    const char* model = "11x10 ESP8266";
    const char* version = "";
};

// Discovery-Telegramm fuer eine Einstellung.
void buildConfigDiscovery(JsonObject out, const ConfigItem& item, const char* prefix,
                          const DeviceInfo& dev);

// Diagnose-Sensoren. Sie landen in HA im ausklappbaren Diagnose-Bereich,
// statt die Karte zu ueberladen.
struct DiagSensor {
    const char* key;
    const char* label;
    const char* unit;         // leer = keine Einheit
    const char* deviceClass;  // leer = keine
};

inline constexpr DiagSensor kDiagSensors[] = {
    {"status", "Status", "", ""},
    {"rssi", "WLAN-Signal", "dBm", "signal_strength"},
    {"heap", "Freier Speicher", "B", "data_size"},
    {"uptime", "Laufzeit", "s", "duration"},
    {"sync_age", "Letzter Zeitsync", "s", "duration"},
    {"current", "Geschaetzter Strom", "mA", "current"},
};
inline constexpr uint8_t kDiagSensorCount = sizeof(kDiagSensors) / sizeof(kDiagSensors[0]);

void buildDiagDiscovery(JsonObject out, const DiagSensor& sensor, const char* prefix,
                        const DeviceInfo& dev);

}  // namespace wordclock
