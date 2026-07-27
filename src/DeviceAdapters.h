#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <time.h>

#include "StripAdapter.h"
#include "TimeSource.h"
#include "WifiConnector.h"
#include "wordclock/ConfigJson.h"
#include "wordclock/Ports.h"

// Die Geraeteseite der Ports. Enthaelt keine Fachlogik -- die liegt in
// wordclock::App und ist auf dem Rechner geprueft.

class DeviceStrip : public wordclock::IStrip {
public:
    explicit DeviceStrip(uint8_t pin) : strip_(pin) {}
    void begin() { strip_.begin(); }
    StripAdapter& raw() { return strip_; }
    void show(const wordclock::Frame& f) override { strip_.show(f); }

private:
    StripAdapter strip_;
};

class DeviceClock : public wordclock::IClock {
public:
    void begin() { time_.begin(); }
    void poll() { time_.tick(millis()); }

    uint32_t nowMs() override { return millis(); }
    bool everSynced() const override { return time_.everSynced(); }
    uint32_t secondsSinceSync() const override { return time_.secondsSinceSync(millis()); }
    void localHm(uint8_t& h, uint8_t& m) const override { time_.localHm(h, m); }

private:
    TimeSource time_;
};

class DeviceNetwork : public wordclock::INetwork {
public:
    void begin(const char* ssid, const char* pass) { wifi_.begin(ssid, pass); }
    void poll() { wifi_.tick(millis()); }

    bool connected() const override { return WiFi.status() == WL_CONNECTED; }
    bool apActive() const override { return false; }  // AP kommt mit der Webapp
    int rssi() const override { return WiFi.RSSI(); }

private:
    WifiConnector wifi_;
};

class DeviceStorage : public wordclock::IStorage {
public:
    static constexpr const char* kPath = "/config.json";
    static constexpr const char* kTmpPath = "/config.tmp";

    void begin() {
        mounted_ = LittleFS.begin();
        if (!mounted_) {
            Serial.println("[cfg] LittleFS nicht einhaengbar, formatiere...");
            mounted_ = LittleFS.format() && LittleFS.begin();
        }
    }

    bool ok() const override { return mounted_; }

    bool load(wordclock::Config& cfg) override {
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

    // Erst Nebendatei, dann umbenennen: ein Stromausfall mitten im Schreiben
    // laesst die alte Konfiguration intakt.
    bool save(wordclock::Config& cfg) override {
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

private:
    bool mounted_ = false;
};

class DeviceSystem : public wordclock::ISystemInfo {
public:
    uint32_t freeHeap() const override { return ESP.getFreeHeap(); }
    void log(const char* line) override { Serial.println(line); }
};

// MQTT ueber PubSubClient.
class DeviceMqtt : public wordclock::IMqttTransport {
public:
    void begin(const char* host, uint16_t port, const char* user, const char* pass) {
        host_ = host;
        port_ = port;
        user_ = user;
        pass_ = pass;
        enabled_ = host && *host;
        if (!enabled_) return;

        instance_ = this;
        client_.setClient(net_);
        client_.setServer(host_, port_);
        client_.setCallback(&DeviceMqtt::trampoline);

        // Discovery-Telegramme sind mehrere hundert Byte. Der Standardpuffer
        // fasst 256 und verwirft Groesseres STILLSCHWEIGEND -- die Entitaet
        // fehlte dann kommentarlos in HomeAssistant.
        client_.setBufferSize(1024);
        client_.setKeepAlive(30);
    }

    bool enabled() const override { return enabled_; }
    bool connected() const override { return enabled_ && const_cast<PubSubClient&>(client_).connected(); }
    void setListener(Listener* l) override { listener_ = l; }
    void setWill(const char* topic, const char* payload) override {
        willTopic_ = topic ? String(topic) : String();
        willPayload_ = payload ? String(payload) : String();
    }

    bool subscribe(const char* topic) override { return client_.subscribe(topic); }

    bool publish(const char* topic, const char* payload, bool retained) override {
        return client_.publish(topic, payload, retained);
    }

    void tick(uint32_t nowMs) override {
        if (!enabled_) return;
        if (client_.connected()) {
            client_.loop();
            return;
        }
        if (int32_t(nowMs - nextAttemptMs_) < 0) return;

        if (attempts_ < 255) ++attempts_;
        char clientId[40];
        snprintf(clientId, sizeof(clientId), "wortuhr-%06X", ESP.getChipId());

        const bool ok = client_.connect(clientId, (user_ && *user_) ? user_ : nullptr,
                                        (pass_ && *pass_) ? pass_ : nullptr,
                                        willTopic_.length() ? willTopic_.c_str() : nullptr, 0,
                                        true, willPayload_.c_str(), true);
        if (ok) {
            attempts_ = 0;
            Serial.println("[mqtt] verbunden");
            if (listener_) listener_->onMqttConnected();
        } else {
            const uint32_t waitMs = attempts_ <= 3 ? 5000 : (attempts_ <= 6 ? 20000 : 60000);
            nextAttemptMs_ = nowMs + waitMs;
            Serial.printf("[mqtt] Versuch %u fehlgeschlagen (rc=%d), naechster in %u s\n",
                          attempts_, client_.state(), unsigned(waitMs / 1000));
        }
    }

private:
    void dispatch(char* topic, uint8_t* payload, unsigned int len) {
        if (!listener_) return;
        char text[512];
        const unsigned int n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
        memcpy(text, payload, n);
        text[n] = '\0';
        listener_->onMqttMessage(topic, text);
    }

    static void trampoline(char* topic, uint8_t* payload, unsigned int len) {
        if (instance_) instance_->dispatch(topic, payload, len);
    }

    static DeviceMqtt* instance_;

    WiFiClient net_;
    PubSubClient client_;
    Listener* listener_ = nullptr;

    const char* host_ = nullptr;
    uint16_t port_ = 1883;
    const char* user_ = nullptr;
    const char* pass_ = nullptr;
    String willTopic_, willPayload_;

    bool enabled_ = false;
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
};

inline DeviceMqtt* DeviceMqtt::instance_ = nullptr;
