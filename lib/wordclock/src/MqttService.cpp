#include "wordclock/MqttService.h"

#include <ArduinoJson.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "wordclock/ConfigJson.h"

namespace wordclock {
namespace {
constexpr size_t kPayloadBuffer = 1024;
}

void MqttService::log(const char* fmt, ...) {
    if (!system_) return;
    char line[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    system_->log(line);
}

void MqttService::begin(IMqttTransport* transport, Config* config, NotifyStack* notify,
                        ISystemInfo* system, const char* prefix) {
    transport_ = transport;
    config_ = config;
    notify_ = notify;
    system_ = system;
    prefix_ = prefix;

    if (!transport_) return;
    transport_->setListener(this);

    char topic[96];
    topicAvailability(topic, sizeof(topic), prefix_);
    transport_->setWill(topic, kPayloadOffline);
}

void MqttService::tick(uint32_t nowMs) {
    if (!transport_) return;
    nowMs_ = nowMs;
    transport_->tick(nowMs);

    if (!transport_->connected()) return;

    // Zustand nur bei tatsaechlicher Aenderung senden, nicht im Takt.
    if (config_ && config_->revision() != publishedRevision_) publishState();
}

void MqttService::onMqttConnected() {
    char topic[96];
    topicAvailability(topic, sizeof(topic), prefix_);
    transport_->publish(topic, kPayloadOnline, /*retained=*/true);

    topicSetWildcard(topic, sizeof(topic), prefix_);
    transport_->subscribe(topic);
    topicNotify(topic, sizeof(topic), prefix_);
    transport_->subscribe(topic);

    publishDiscovery();

    // Nach dem Verbinden muss der Zustand in jedem Fall raus, auch wenn sich
    // nichts geaendert hat -- der Broker koennte ihn verloren haben.
    publishedRevision_ = 0xFFFFFFFFu;
    publishState();
}

void MqttService::publishDiscovery() {
    DeviceInfo dev;
    char topic[128];
    char payload[kPayloadBuffer];

    for (uint8_t i = 0; i < kConfigCount; ++i) {
        JsonDocument doc;
        buildConfigDiscovery(doc.to<JsonObject>(), kSchema[i], prefix_, dev);
        topicDiscovery(topic, sizeof(topic), componentFor(kSchema[i].type), dev.id,
                       kSchema[i].key);
        serializeJson(doc, payload, sizeof(payload));
        transport_->publish(topic, payload, true) ? ++stats_.discoverySent
                                                  : ++stats_.discoveryFailed;
    }

    for (uint8_t i = 0; i < kDiagSensorCount; ++i) {
        JsonDocument doc;
        buildDiagDiscovery(doc.to<JsonObject>(), kDiagSensors[i], prefix_, dev);
        topicDiscovery(topic, sizeof(topic), "sensor", dev.id, kDiagSensors[i].key);
        serializeJson(doc, payload, sizeof(payload));
        transport_->publish(topic, payload, true) ? ++stats_.discoverySent
                                                  : ++stats_.discoveryFailed;
    }

    log("[mqtt] Discovery: %u gesendet, %u fehlgeschlagen", stats_.discoverySent,
        stats_.discoveryFailed);
}

void MqttService::publishState() {
    if (!config_ || !transport_) return;

    JsonDocument doc;
    toJson(*config_, doc.to<JsonObject>());

    char payload[kPayloadBuffer];
    serializeJson(doc, payload, sizeof(payload));

    char topic[96];
    topicState(topic, sizeof(topic), prefix_);
    if (transport_->publish(topic, payload, /*retained=*/true)) {
        publishedRevision_ = config_->revision();
        ++stats_.statePublished;
    }
}

void MqttService::publishDiagnostics(uint32_t nowMs, const Diagnostics& d) {
    if (!connected()) return;
    if (lastDiagMs_ != 0 && nowMs - lastDiagMs_ < kDiagIntervalMs) return;
    lastDiagMs_ = nowMs;

    JsonDocument doc;
    doc["status"] = d.status;
    doc["rssi"] = d.rssi;
    doc["heap"] = d.heap;
    doc["uptime"] = d.uptimeS;
    doc["sync_age"] = d.syncAgeS;
    doc["current"] = d.currentMa;

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    char topic[96];
    topicDiag(topic, sizeof(topic), prefix_);
    transport_->publish(topic, payload, /*retained=*/true);
}

void MqttService::onMqttMessage(const char* topic, const char* payload) {
    char notifyTopic[96];
    topicNotify(notifyTopic, sizeof(notifyTopic), prefix_);
    if (!std::strcmp(topic, notifyTopic)) {
        handleNotify(payload);
        return;
    }

    char key[48];
    if (!keyFromSetTopic(topic, prefix_, key, sizeof(key))) return;
    handleCommand(key, payload);
}

void MqttService::handleCommand(const char* key, const char* payload) {
    if (!config_) return;

    ConfigKey k;
    if (!Config::keyIndex(key, k)) {
        ++stats_.commandsRejected;
        log("[mqtt] unbekannte Einstellung: %s", key);
        return;
    }

    int32_t value = 0;
    if (!parseValue(schemaOf(k), payload, value)) {
        ++stats_.commandsRejected;
        log("[mqtt] %s: \"%s\" nicht lesbar", key, payload);
        return;
    }

    // Der Rueckgabewert meldet nur, ob geklemmt werden musste. Uebernommen
    // wird immer -- die Uhr veroeffentlicht danach, was wirklich gilt
    // (DESIGN 8.1).
    if (!config_->set(k, value)) {
        ++stats_.commandsClamped;
        log("[mqtt] %s auf gueltigen Bereich geklemmt", key);
    }
    ++stats_.commandsApplied;
}

void MqttService::handleNotify(const char* payload) {
    if (!notify_) return;

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        ++stats_.notifyRejected;
        log("[mqtt] notify: JSON kaputt");
        return;
    }

    const char* id = doc["id"];
    if (!id || !*id) {
        ++stats_.notifyRejected;
        log("[mqtt] notify ohne id");
        return;
    }

    if (doc["clear"].as<bool>()) {
        notify_->clear(id);
        ++stats_.notifyApplied;
        log("[notify] %s geloescht", id);
        return;
    }

    NotifyRequest req;
    req.id = id;
    req.prio = doc["prio"] | 50;
    req.ttlSeconds = doc["ttl"] | 0;

    const char* style = doc["style"] | "tint";
    if (!parseStyle(style, req.style)) {
        ++stats_.notifyRejected;
        log("[mqtt] notify: unbekannter Style %s", style);
        return;
    }

    if (doc["color"].is<JsonArray>()) {
        JsonArray c = doc["color"];
        if (c.size() >= 3)
            req.color = {c[0].as<uint8_t>(), c[1].as<uint8_t>(), c[2].as<uint8_t>()};
    }

    // Rasterwort per Name -- Indizes waeren beim Umsortieren nicht stabil.
    if (doc["word"].is<const char*>()) {
        const char* want = doc["word"];
        for (uint8_t i = 0; i < kWordCount; ++i) {
            if (!std::strcmp(want, kWordText[i])) {
                req.word = i;
                break;
            }
        }
    }

    const char* anim = doc["anim"];
    if (anim) req.anim = anim;

    // glyph: Format und Kodierung sind noch offen (DESIGN 11).
    if (!doc["glyph"].isNull()) log("[mqtt] notify: glyph noch nicht umgesetzt");

    if (notify_->push(req, nowMs_)) {
        ++stats_.notifyApplied;
        log("[notify] %s prio=%u ttl=%us", req.id, req.prio, unsigned(req.ttlSeconds));
    } else {
        ++stats_.notifyRejected;
        log("[notify] %s abgewiesen, Stapel voll", req.id);
    }
}

}  // namespace wordclock
