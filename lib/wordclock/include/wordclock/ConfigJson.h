#pragma once

#include <ArduinoJson.h>

#include "wordclock/Config.h"

namespace wordclock {

// Alle Werte als flaches JSON. Das ist zugleich der Dateiinhalt im LittleFS und
// die Grundlage der State-Topics.
void toJson(const Config& cfg, JsonObject out);

// Uebernimmt bekannte Schluessel, ignoriert unbekannte, klemmt Ungueltiges.
//
// Absichtlich fehlertolerant: eine Konfigurationsdatei aus einer aelteren
// Firmware darf nicht dazu fuehren, dass die Uhr gar nicht mehr startet. Was
// fehlt, behaelt seine Vorgabe.
struct LoadReport {
    uint8_t applied = 0;   // uebernommene Schluessel
    uint8_t unknown = 0;   // unbekannte Schluessel im JSON
    uint8_t clamped = 0;   // Werte, die geklemmt werden mussten
    uint8_t missing = 0;   // Schluessel, die im JSON fehlten
};

LoadReport fromJson(Config& cfg, JsonObjectConst in);

// Die Beschreibung selbst -- daraus baut die Webapp ihr Formular, ohne dass das
// Geraet HTML erzeugen muesste.
void schemaToJson(JsonArray out);

}  // namespace wordclock
