#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "wordclock/ConfigJson.h"

// Persistenz der Konfiguration im LittleFS.
//
// Duenner Adapter ohne Fachlogik: Validierung, Vorgaben und Fehlertoleranz
// stecken in Config und ConfigJson und sind dort ohne Hardware geprueft.
class ConfigStore {
public:
    static constexpr const char* kPath = "/config.json";
    static constexpr const char* kTmpPath = "/config.tmp";

    // true, wenn das Dateisystem benutzbar ist. Schlaegt es fehl, meldet der
    // Gesundheitskanal Code 3 -- die Uhr laeuft mit Vorgaben weiter.
    bool begin() {
        mounted_ = LittleFS.begin();
        if (!mounted_) {
            Serial.println("[cfg] LittleFS nicht einhaengbar, formatiere...");
            mounted_ = LittleFS.format() && LittleFS.begin();
        }
        return mounted_;
    }

    bool mounted() const { return mounted_; }

    // Laedt, was da ist. Fehlt die Datei oder ist sie unlesbar, bleiben die
    // Vorgaben stehen -- das ist kein Fehler, sondern der erste Start.
    bool load(wordclock::Config& cfg) {
        if (!mounted_ || !LittleFS.exists(kPath)) {
            Serial.println("[cfg] keine Datei, benutze Vorgaben");
            return false;
        }

        File f = LittleFS.open(kPath, "r");
        if (!f) return false;

        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, f);
        f.close();

        if (err) {
            Serial.printf("[cfg] JSON kaputt (%s), benutze Vorgaben\n", err.c_str());
            return false;
        }

        const wordclock::LoadReport rep = fromJson(cfg, doc.as<JsonObjectConst>());
        Serial.printf("[cfg] geladen: %u uebernommen, %u fehlend, %u geklemmt, %u unbekannt\n",
                      rep.applied, rep.missing, rep.clamped, rep.unknown);
        cfg.clearDirty();
        return true;
    }

    // Erst in eine Nebendatei schreiben, dann umbenennen. Ein Stromausfall
    // mitten im Schreiben laesst so die alte Konfiguration intakt, statt eine
    // halbe Datei zu hinterlassen.
    bool save(wordclock::Config& cfg) {
        if (!mounted_) return false;

        JsonDocument doc;
        toJson(cfg, doc.to<JsonObject>());

        File f = LittleFS.open(kTmpPath, "w");
        if (!f) return false;
        const size_t written = serializeJson(doc, f);
        f.close();

        if (written == 0) {
            LittleFS.remove(kTmpPath);
            return false;
        }

        LittleFS.remove(kPath);
        if (!LittleFS.rename(kTmpPath, kPath)) return false;

        cfg.clearDirty();
        Serial.printf("[cfg] gespeichert, %u Byte\n", unsigned(written));
        return true;
    }

    // Regelmaessig aufrufen: speichert nur, wenn sich etwas geaendert hat, und
    // erst nach einer Ruhephase. So erzeugt ein Schieberegler in HomeAssistant
    // nicht bei jedem Zwischenwert einen Flash-Schreibvorgang.
    void tickAutosave(wordclock::Config& cfg, uint32_t nowMs, uint32_t quietMs = 3000) {
        const uint32_t rev = cfg.revision();
        if (rev != lastRevision_) {  // es hat sich gerade wieder etwas geruehrt
            lastRevision_ = rev;
            lastChangeMs_ = nowMs;
            return;
        }
        if (cfg.dirty() && (nowMs - lastChangeMs_) >= quietMs) save(cfg);
    }

private:
    bool mounted_ = false;
    uint32_t lastRevision_ = 0;
    uint32_t lastChangeMs_ = 0;
};
