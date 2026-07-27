#pragma once

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <ArduinoJson.h>

#include "TerminalPanel.h"
#include "wordclock/ConfigJson.h"
#include "wordclock/Ports.h"

namespace sim {

// Die Rechnerseite der Ports. Gegenstueck zu src/DeviceAdapters.h.

// --- Panel -----------------------------------------------------------------

class TerminalStrip : public wordclock::IStrip {
public:
    void show(const wordclock::Frame& f) override { last_ = f; }
    const wordclock::Frame& last() const { return last_; }

private:
    wordclock::Frame last_;
};

// --- Zeit ------------------------------------------------------------------

// Bewusst zwei getrennte Zeitachsen:
//
//   nowMs      laeuft in Echtzeit -- sonst waeren Uebergaenge und Blinken im
//              Zeitraffer nicht mehr zu beurteilen.
//   Wanduhr    laeuft um `speed` beschleunigt -- ein Tag-Nacht-Wechsel soll
//              nicht acht Stunden dauern.
//
// Das ist keine Schummelei, sondern genau die Trennung, die die Firmware
// ohnehin macht: Bewegung haengt an millis(), die Anzeige an der Uhrzeit.
class SimClock : public wordclock::IClock {
public:
    void begin(uint8_t startHour, uint8_t startMinute, double speed, bool synced) {
        wallSeconds_ = double(startHour) * 3600 + double(startMinute) * 60;
        speed_ = speed;
        synced_ = synced;
        start_ = std::chrono::steady_clock::now();
        lastPoll_ = start_;
    }

    void poll() {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - lastPoll_).count();
        lastPoll_ = now;
        wallSeconds_ += dt * speed_;
        while (wallSeconds_ >= 86400.0) wallSeconds_ -= 86400.0;
    }

    void setSynced(bool on) { synced_ = on; }
    void setSyncAge(uint32_t seconds) { syncAge_ = seconds; }

    uint32_t nowMs() override {
        const auto now = std::chrono::steady_clock::now();
        return uint32_t(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count());
    }

    bool everSynced() const override { return synced_; }
    uint32_t secondsSinceSync() const override { return syncAge_; }

    void localHm(uint8_t& hours, uint8_t& minutes) const override {
        const long total = long(wallSeconds_);
        hours = uint8_t((total / 3600) % 24);
        minutes = uint8_t((total / 60) % 60);
    }

private:
    double wallSeconds_ = 0;
    double speed_ = 1.0;
    bool synced_ = true;
    uint32_t syncAge_ = 60;
    std::chrono::steady_clock::time_point start_, lastPoll_;
};

// --- Netz ------------------------------------------------------------------

class SimNetwork : public wordclock::INetwork {
public:
    void set(bool connected, bool ap = false) {
        connected_ = connected;
        ap_ = ap;
    }
    bool connected() const override { return connected_; }
    bool apActive() const override { return ap_; }
    int rssi() const override { return connected_ ? -58 : 0; }

private:
    bool connected_ = true;
    bool ap_ = false;
};

// --- Speicher --------------------------------------------------------------

// Echte Datei auf der Platte, gleiche Semantik wie LittleFS auf dem Geraet:
// erst Nebendatei, dann umbenennen.
class FileStorage : public wordclock::IStorage {
public:
    explicit FileStorage(std::string path) : path_(std::move(path)) {}

    bool ok() const override { return ok_; }

    bool load(wordclock::Config& cfg) override {
        std::ifstream in(path_);
        if (!in) {
            std::printf("[cfg] %s nicht vorhanden, benutze Vorgaben\n", path_.c_str());
            return false;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();

        JsonDocument doc;
        if (deserializeJson(doc, text)) {
            std::printf("[cfg] JSON kaputt, benutze Vorgaben\n");
            return false;
        }
        const wordclock::LoadReport rep = fromJson(cfg, doc.as<JsonObjectConst>());
        std::printf("[cfg] geladen: %u uebernommen, %u fehlend, %u geklemmt, %u unbekannt\n",
                    rep.applied, rep.missing, rep.clamped, rep.unknown);
        cfg.clearDirty();
        return true;
    }

    bool save(wordclock::Config& cfg) override {
        JsonDocument doc;
        toJson(cfg, doc.to<JsonObject>());
        std::string text;
        serializeJson(doc, text);

        const std::string tmp = path_ + ".tmp";
        {
            std::ofstream out(tmp);
            if (!out) return false;
            out << text;
        }
        std::remove(path_.c_str());
        if (std::rename(tmp.c_str(), path_.c_str()) != 0) return false;

        cfg.clearDirty();
        std::printf("[cfg] gespeichert, %zu Byte\n", text.size());
        return true;
    }

private:
    std::string path_;
    bool ok_ = true;
};

// --- System ----------------------------------------------------------------

class SimSystem : public wordclock::ISystemInfo {
public:
    uint32_t freeHeap() const override { return 44000; }  // plausibler Wert vom Geraet
    void log(const char* line) override { logs_.emplace_back(line); }

    std::vector<std::string>& logs() { return logs_; }

private:
    std::vector<std::string> logs_;
};

}  // namespace sim
