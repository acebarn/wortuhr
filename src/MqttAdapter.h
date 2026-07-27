#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "wordclock/ConfigJson.h"
#include "wordclock/MqttContract.h"
#include "wordclock/Notify.h"

// MQTT-Anbindung an HomeAssistant.
//
// Duenner Adapter: Topics, Wertumwandlung und Discovery-Telegramme kommen aus
// wordclock/MqttContract und sind ohne Netzwerk geprueft. Hier liegt nur die
// Verbindung, das Wiederverbinden und die Zuordnung eingehender Nachrichten.
//
// HOHEIT (DESIGN 8.1): Befehle sind Anfragen. Die Uhr entscheidet, klemmt und
// veroeffentlicht danach ihren Zustand. Command-Topics werden nie retained
// abonniert oder gesendet -- ein retained Befehl wuerde bei jedem Start erneut
// ausgeliefert.

class MqttAdapter {
public:
    using NotifyHandler = void (*)(const wordclock::NotifyRequest&, bool clear);

    void begin(const char* host, uint16_t port, const char* user, const char* pass,
               const char* prefix, wordclock::Config* cfg, NotifyHandler onNotify) {
        host_ = host;
        port_ = port;
        user_ = user;
        pass_ = pass;
        prefix_ = prefix;
        config_ = cfg;
        onNotify_ = onNotify;
        enabled_ = host && *host;

        if (!enabled_) return;

        instance_ = this;
        client_.setClient(net_);
        client_.setServer(host_, port_);
        client_.setCallback(&MqttAdapter::trampoline);

        // Discovery-Telegramme sind mehrere hundert Byte. Der Standardpuffer
        // von PubSubClient fasst 256 und verwirft Groesseres STILLSCHWEIGEND --
        // die Entitaet fehlt dann einfach in HomeAssistant.
        client_.setBufferSize(1024);
        client_.setKeepAlive(30);
    }

    bool enabled() const { return enabled_; }
    bool connected() { return enabled_ && client_.connected(); }

    void tick(uint32_t nowMs) {
        if (!enabled_) return;

        if (!client_.connected()) {
            if (int32_t(nowMs - nextAttemptMs_) >= 0) attempt(nowMs);
            return;
        }

        client_.loop();

        // Zustand nur bei tatsaechlicher Aenderung senden, nicht im Takt.
        if (config_ && config_->revision() != publishedRevision_) publishState(nowMs);
    }

    // Wird vom Hauptprogramm mit den aktuellen Messwerten gefuettert.
    void publishDiag(uint32_t nowMs, const char* status, int rssi, uint32_t heap,
                     uint32_t uptimeS, uint32_t syncAgeS, uint32_t currentMa) {
        if (!connected()) return;
        if (nowMs - lastDiagMs_ < kDiagIntervalMs) return;
        lastDiagMs_ = nowMs;

        JsonDocument doc;
        doc["status"] = status;
        doc["rssi"] = rssi;
        doc["heap"] = heap;
        doc["uptime"] = uptimeS;
        doc["sync_age"] = syncAgeS;
        doc["current"] = currentMa;

        char topic[80];
        wordclock::topicDiag(topic, sizeof(topic), prefix_);
        publishJson(topic, doc, /*retained=*/true);
    }

private:
    static constexpr uint32_t kDiagIntervalMs = 30000;

    void attempt(uint32_t nowMs) {
        if (attempts_ < 255) ++attempts_;

        char availability[80];
        wordclock::topicAvailability(availability, sizeof(availability), prefix_);

        char clientId[40];
        std::snprintf(clientId, sizeof(clientId), "%s-%06X", prefix_, ESP.getChipId());

        const bool ok = client_.connect(clientId, (user_ && *user_) ? user_ : nullptr,
                                        (pass_ && *pass_) ? pass_ : nullptr, availability,
                                        /*willQos=*/0, /*willRetain=*/true,
                                        wordclock::kPayloadOffline,
                                        /*cleanSession=*/true);

        if (ok) {
            attempts_ = 0;
            Serial.println("[mqtt] verbunden");
            client_.publish(availability, wordclock::kPayloadOnline, /*retained=*/true);
            subscribe();
            publishDiscovery();
            publishState(nowMs);
        } else {
            const uint32_t waitMs = attempts_ <= 3 ? 5000 : (attempts_ <= 6 ? 20000 : 60000);
            nextAttemptMs_ = nowMs + waitMs;
            Serial.printf("[mqtt] Versuch %u fehlgeschlagen (rc=%d), naechster in %u s\n",
                          attempts_, client_.state(), unsigned(waitMs / 1000));
        }
    }

    void subscribe() {
        char topic[80];
        wordclock::topicSetWildcard(topic, sizeof(topic), prefix_);
        client_.subscribe(topic);
        wordclock::topicNotify(topic, sizeof(topic), prefix_);
        client_.subscribe(topic);
    }

    void publishDiscovery() {
        using namespace wordclock;
        DeviceInfo dev;
        dev.version = __DATE__;

        char topic[128];
        uint8_t sent = 0, failed = 0;

        for (uint8_t i = 0; i < kConfigCount; ++i) {
            JsonDocument doc;
            buildConfigDiscovery(doc.to<JsonObject>(), kSchema[i], prefix_, dev);
            topicDiscovery(topic, sizeof(topic), componentFor(kSchema[i].type), dev.id,
                           kSchema[i].key);
            publishJson(topic, doc, true) ? ++sent : ++failed;
            delay(5);  // dem Broker Luft lassen
        }

        for (uint8_t i = 0; i < kDiagSensorCount; ++i) {
            JsonDocument doc;
            buildDiagDiscovery(doc.to<JsonObject>(), kDiagSensors[i], prefix_, dev);
            topicDiscovery(topic, sizeof(topic), "sensor", dev.id, kDiagSensors[i].key);
            publishJson(topic, doc, true) ? ++sent : ++failed;
            delay(5);
        }

        Serial.printf("[mqtt] Discovery: %u gesendet, %u fehlgeschlagen\n", sent, failed);
    }

    void publishState(uint32_t nowMs) {
        if (!config_) return;
        (void)nowMs;

        JsonDocument doc;
        toJson(*config_, doc.to<JsonObject>());

        char topic[80];
        wordclock::topicState(topic, sizeof(topic), prefix_);
        if (publishJson(topic, doc, /*retained=*/true)) publishedRevision_ = config_->revision();
    }

    bool publishJson(const char* topic, const JsonDocument& doc, bool retained) {
        const size_t len = measureJson(doc);
        if (len + 2 > client_.getBufferSize()) {
            Serial.printf("[mqtt] Telegramm zu gross fuer %s (%u Byte)\n", topic, unsigned(len));
            return false;
        }
        char buf[1024];
        serializeJson(doc, buf, sizeof(buf));
        return client_.publish(topic, buf, retained);
    }

    void onMessage(const char* topic, const uint8_t* payload, unsigned int len) {
        char text[256];
        const unsigned int n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
        std::memcpy(text, payload, n);
        text[n] = '\0';

        char notifyTopic[80];
        wordclock::topicNotify(notifyTopic, sizeof(notifyTopic), prefix_);
        if (!std::strcmp(topic, notifyTopic)) {
            handleNotify(text);
            return;
        }

        char key[40];
        if (!wordclock::keyFromSetTopic(topic, prefix_, key, sizeof(key))) return;

        wordclock::ConfigKey k;
        if (!wordclock::Config::keyIndex(key, k)) {
            Serial.printf("[mqtt] unbekannte Einstellung: %s\n", key);
            return;
        }

        int32_t value = 0;
        if (!wordclock::parseValue(wordclock::schemaOf(k), text, value)) {
            Serial.printf("[mqtt] %s: \"%s\" nicht lesbar\n", key, text);
            return;
        }

        // Der Rueckgabewert meldet nur, ob geklemmt werden musste. Uebernommen
        // wird immer -- die Uhr veroeffentlicht danach, was wirklich gilt.
        if (!config_->set(k, value))
            Serial.printf("[mqtt] %s auf gueltigen Bereich geklemmt\n", key);
    }

    void handleNotify(const char* json) {
        if (!onNotify_) return;

        JsonDocument doc;
        if (deserializeJson(doc, json)) {
            Serial.println("[mqtt] notify: JSON kaputt");
            return;
        }

        wordclock::NotifyRequest req;
        const char* id = doc["id"];
        if (!id || !*id) {
            Serial.println("[mqtt] notify ohne id");
            return;
        }
        req.id = id;

        if (doc["clear"].as<bool>()) {
            onNotify_(req, /*clear=*/true);
            return;
        }

        req.prio = doc["prio"] | 50;
        req.ttlSeconds = doc["ttl"] | 0;

        const char* style = doc["style"] | "tint";
        if (!wordclock::parseStyle(style, req.style)) {
            Serial.printf("[mqtt] notify: unbekannter Style %s\n", style);
            return;
        }

        if (doc["color"].is<JsonArray>()) {
            JsonArray c = doc["color"];
            if (c.size() >= 3)
                req.color = {c[0].as<uint8_t>(), c[1].as<uint8_t>(), c[2].as<uint8_t>()};
        }

        if (doc["word"].is<const char*>()) {
            // Rasterwort per Name -- Indizes waeren nicht stabil.
            const char* want = doc["word"];
            for (uint8_t i = 0; i < wordclock::kWordCount; ++i) {
                if (!std::strcmp(want, wordclock::kWordText[i])) {
                    req.word = i;
                    break;
                }
            }
        }

        const char* anim = doc["anim"];
        if (anim) req.anim = anim;

        // glyph: Format und Kodierung sind noch offen (DESIGN 11).
        if (!doc["glyph"].isNull()) Serial.println("[mqtt] notify: glyph noch nicht umgesetzt");

        onNotify_(req, /*clear=*/false);
    }

    static void trampoline(char* topic, uint8_t* payload, unsigned int len) {
        if (instance_) instance_->onMessage(topic, payload, len);
    }

    static MqttAdapter* instance_;

    WiFiClient net_;
    PubSubClient client_;

    const char* host_ = nullptr;
    uint16_t port_ = 1883;
    const char* user_ = nullptr;
    const char* pass_ = nullptr;
    const char* prefix_ = wordclock::kDefaultPrefix;

    wordclock::Config* config_ = nullptr;
    NotifyHandler onNotify_ = nullptr;

    bool enabled_ = false;
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
    uint32_t lastDiagMs_ = 0;
    uint32_t publishedRevision_ = 0xFFFFFFFF;
};

inline MqttAdapter* MqttAdapter::instance_ = nullptr;
