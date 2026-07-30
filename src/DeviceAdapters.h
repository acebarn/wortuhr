#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <time.h>

#include "StripAdapter.h"
#include "TimeSource.h"
#include "WifiConnector.h"
#include "wordclock/ConfigJson.h"
#include "wordclock/Ports.h"
#include "wordclock/WebApi.h"

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

// WLAN mit additivem Accesspoint (DESIGN 9.1).
//
// Der AP ERSETZT den Verbindungsversuch nicht, er kommt dazu: kommt der Router
// zurueck, faengt die Uhr sich selbst wieder ein. Und es wird nie blockiert --
// loop() wird immer erreicht, auch ohne Netz.
class DeviceNetwork : public wordclock::INetwork {
public:
    static constexpr uint8_t kAttemptsBeforeAp = 7;

    void applyCredentials(const char* ssid, const char* pass) override {
        ssid_ = ssid ? ssid : "";
        pass_ = pass ? pass : "";
        attempts_ = 0;
        nextAttemptMs_ = 0;

        WiFi.persistent(false);
        WiFi.hostname("wortuhr");

        if (ssid_.isEmpty()) {
            // Ohne Zugangsdaten hat Warten keinen Sinn -- direkt einrichten
            // lassen.
            openAp();
            return;
        }
        WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
        WiFi.begin(ssid_.c_str(), pass_.c_str());
        Serial.printf("[wifi] verbinde mit \"%s\"\n", ssid_.c_str());
    }

    void poll() {
        const uint32_t nowMs = millis();
        if (connected()) {
            if (attempts_) {
                Serial.printf("[wifi] verbunden, %s\n", WiFi.localIP().toString().c_str());
                attempts_ = 0;
            }
            return;
        }
        if (ssid_.isEmpty() || int32_t(nowMs - nextAttemptMs_) < 0) return;

        if (attempts_ < 255) ++attempts_;
        const uint32_t waitMs = attempts_ <= 3 ? 10000 : (attempts_ <= 6 ? 30000 : 120000);
        nextAttemptMs_ = nowMs + waitMs;

        if (attempts_ >= kAttemptsBeforeAp && !apActive_) openAp();

        WiFi.begin(ssid_.c_str(), pass_.c_str());
        Serial.printf("[wifi] Versuch %u, naechster in %u s%s\n", attempts_,
                      unsigned(waitMs / 1000), apActive_ ? " (AP offen)" : "");
    }

    bool connected() const override { return WiFi.status() == WL_CONNECTED; }
    bool apActive() const override { return apActive_; }
    int rssi() const override { return WiFi.RSSI(); }

    const char* ip() const override {
        static char buf[24];
        const IPAddress a = connected() ? WiFi.localIP() : WiFi.softAPIP();
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
        return buf;
    }

private:
    void openAp() {
        apActive_ = true;
        WiFi.mode(ssid_.isEmpty() ? WIFI_AP : WIFI_AP_STA);
        WiFi.softAP("Wortuhr-Setup");
        Serial.printf("[wifi] AP offen: Wortuhr-Setup, %s\n", WiFi.softAPIP().toString().c_str());
    }

    String ssid_, pass_;
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
    bool apActive_ = false;
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

    bool loadSecrets(wordclock::Secrets& sec) override {
        if (!mounted_ || !LittleFS.exists(kSecretPath)) return false;
        File f = LittleFS.open(kSecretPath, "r");
        if (!f) return false;
        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) return false;

        uint8_t n = 0;
        for (JsonPairConst kv : doc.as<JsonObjectConst>())
            if (kv.value().is<const char*>() &&
                sec.setByName(kv.key().c_str(), kv.value().as<const char*>()))
                ++n;
        sec.clearDirty();
        Serial.printf("[cfg] Zugangsdaten geladen (%u)\n", n);
        return true;
    }

    bool saveSecrets(wordclock::Secrets& sec) override {
        if (!mounted_) return false;
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        for (uint8_t i = 0; i < wordclock::kSecretCount; ++i)
            o[wordclock::kSecretSchema[i].key] = sec.get(wordclock::SecretKey(i));

        File f = LittleFS.open(kSecretTmp, "w");
        if (!f) return false;
        const size_t written = serializeJson(doc, f);
        f.close();
        if (!written) { LittleFS.remove(kSecretTmp); return false; }

        LittleFS.remove(kSecretPath);
        if (!LittleFS.rename(kSecretTmp, kSecretPath)) return false;
        sec.clearDirty();
        Serial.println("[cfg] Zugangsdaten gespeichert");
        return true;
    }

private:
    static constexpr const char* kSecretPath = "/secrets.json";
    static constexpr const char* kSecretTmp = "/secrets.tmp";
    bool mounted_ = false;
};

class DeviceSystem : public wordclock::ISystemInfo {
public:
    uint32_t freeHeap() const override { return ESP.getFreeHeap(); }
    void log(const char* line) override { Serial.println(line); }
    void restart() override {
        Serial.println("[sys] Neustart");
        delay(200);
        ESP.restart();
    }
};

// MQTT ueber PubSubClient.
class DeviceMqtt : public wordclock::IMqttTransport {
public:
    void applyBroker(const char* host, uint16_t port, const char* user,
                     const char* pass) override {
        host_ = host ? host : "";
        port_ = port;
        user_ = user ? user : "";
        pass_ = pass ? pass : "";
        enabled_ = !host_.isEmpty();

        if (client_.connected()) client_.disconnect();
        attempts_ = 0;
        nextAttemptMs_ = 0;
        if (!enabled_) return;

        instance_ = this;
        client_.setServer(host_.c_str(), port_);
        Serial.printf("[mqtt] Broker %s:%u\n", host_.c_str(), port_);
    }

    void begin() {
        instance_ = this;
        client_.setClient(net_);
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

        const bool ok = client_.connect(clientId, user_.isEmpty() ? nullptr : user_.c_str(),
                                        pass_.isEmpty() ? nullptr : pass_.c_str(),
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

    String host_, user_, pass_;
    uint16_t port_ = 1883;
    String willTopic_, willPayload_;

    bool enabled_ = false;
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
};

inline DeviceMqtt* DeviceMqtt::instance_ = nullptr;

// --- Webserver -------------------------------------------------------------
//
// Immer erreichbar, nicht nur im AP-Modus. Der wahrscheinlichere Stoerfall ist
// nicht "WLAN weg", sondern "Broker weg bei intaktem WLAN" -- und dann muss die
// Uhr trotzdem konfigurierbar bleiben (DESIGN 8.3).

class DeviceWeb {
public:
    void begin(wordclock::WebApi* api) {
        api_ = api;
        server_.onNotFound([this]() { handle(); });
        server_.begin();

        // wortuhr.local -- damit nie eine IP-Adresse gemerkt werden muss.
        if (MDNS.begin("wortuhr")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("[web] http://wortuhr.local");
        }
    }

    void poll() {
        server_.handleClient();
        MDNS.update();
    }

    // Nach dem Senden der Antwort ausgefuehrt -- sonst saehe der Browser nie
    // eine Bestaetigung.
    wordclock::WebAction takeAction() {
        const wordclock::WebAction a = pending_;
        pending_ = wordclock::WebAction::None;
        return a;
    }

private:
    void handle() {
        if (!api_) return;

        const String path = server_.uri();
        const String body = server_.hasArg("plain") ? server_.arg("plain") : String();
        wordclock::WebRequest req{server_.method() == HTTP_POST ? "POST" : "GET", path.c_str(),
                                  body.c_str()};
        wordclock::WebResponse res;
        pending_ = api_->handle(req, res, buffer_, sizeof(buffer_));

        if (res.serveIndexPage) {
            server_.send_P(200, "text/html", wordclock::WebApi::indexPage());
            return;
        }
        server_.send(res.status, res.contentType, res.body ? res.body : "");
    }

    ESP8266WebServer server_{80};
    wordclock::WebApi* api_ = nullptr;
    wordclock::WebAction pending_ = wordclock::WebAction::None;
    char buffer_[wordclock::kWebBufferSize] = {};
};

// --- Notzugang ueber den Stecker -------------------------------------------
//
// Die Uhr hat kein Bedienelement. Fuenf kurz hintereinander abgebrochene
// Startvorgaenge loesen deshalb Werksreset und AP aus (DESIGN 9.2). Der Zaehler
// verfaellt nach 10 s Laufzeit -- versehentliches Ausloesen ist damit
// unwahrscheinlich, und die Eckpunkte zeigen den Stand.
class BootGuard {
public:
    static constexpr uint8_t kTrigger = 5;
    static constexpr uint32_t kQuietMs = 10000;
    static constexpr const char* kPath = "/boot.cnt";

    // Gibt den neuen Zaehlerstand zurueck.
    uint8_t begin() {
        count_ = read();
        if (count_ < 250) ++count_;
        write(count_);
        Serial.printf("[boot] Zaehler %u von %u\n", count_, kTrigger);
        return count_;
    }

    bool triggered() const { return count_ >= kTrigger; }

    void poll(uint32_t nowMs) {
        if (cleared_ || nowMs < kQuietMs) return;
        cleared_ = true;
        write(0);
        Serial.println("[boot] Zaehler zurueckgesetzt");
    }

private:
    static uint8_t read() {
        File f = LittleFS.open(kPath, "r");
        if (!f) return 0;
        const int c = f.read();
        f.close();
        return c > 0 ? uint8_t(c) : 0;
    }
    static void write(uint8_t v) {
        File f = LittleFS.open(kPath, "w");
        if (!f) return;
        f.write(v);
        f.close();
    }

    uint8_t count_ = 0;
    bool cleared_ = false;
};
