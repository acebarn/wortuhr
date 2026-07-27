#include "wordclock/WebApi.h"

#include <ArduinoJson.h>

#include <cstring>

#include "wordclock/ConfigJson.h"
#include "wordclock/MqttContract.h"

namespace wordclock {
namespace {

bool isPath(const WebRequest& r, const char* p) { return !std::strcmp(r.path, p); }
bool isPost(const WebRequest& r) { return !std::strcmp(r.method, "POST"); }

const char* typeName(ConfigType t) {
    switch (t) {
        case ConfigType::Bool: return "bool";
        case ConfigType::Number: return "number";
        case ConfigType::Color: return "color";
        case ConfigType::Choice: return "choice";
        case ConfigType::TimeOfDay: return "time";
    }
    return "number";
}

// Schreibt das Dokument in den Puffer. Passt es nicht, meldet sie das, statt
// abzuschneiden -- halbes JSON waere schlimmer als ein Fehler.
bool writeJson(const JsonDocument& doc, WebResponse& res, char* buffer, size_t bufferSize) {
    const size_t needed = measureJson(doc);
    if (needed + 1 > bufferSize) {
        res.status = 507;
        res.contentType = "text/plain";
        std::snprintf(buffer, bufferSize, "Antwort zu gross (%u Byte)", unsigned(needed));
        res.body = buffer;
        res.length = std::strlen(buffer);
        return false;
    }
    res.length = serializeJson(doc, buffer, bufferSize);
    res.body = buffer;
    res.status = 200;
    res.contentType = "application/json";
    return true;
}

void writeText(WebResponse& res, char* buffer, size_t bufferSize, int status, const char* text) {
    res.status = status;
    res.contentType = "text/plain";
    std::snprintf(buffer, bufferSize, "%s", text);
    res.body = buffer;
    res.length = std::strlen(buffer);
}

}  // namespace

WebAction WebApi::handle(const WebRequest& req, WebResponse& res, char* buffer,
                         size_t bufferSize) {
    res = WebResponse{};

    // --- Seite ------------------------------------------------------------
    if (isPath(req, "/") || isPath(req, "/index.html")) {
        res.serveIndexPage = true;
        res.contentType = "text/html";
        return WebAction::None;
    }

    // --- Beschreibung der Einstellungen -----------------------------------
    if (isPath(req, "/api/schema")) {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        JsonArray settings = root["settings"].to<JsonArray>();
        for (uint8_t i = 0; i < kConfigCount; ++i) {
            const ConfigItem& it = kSchema[i];
            JsonObject o = settings.add<JsonObject>();
            o["key"] = it.key;
            o["label"] = it.label;
            o["category"] = it.category;
            o["type"] = typeName(it.type);
            o["min"] = it.min;
            o["max"] = it.max;
            if (it.options && it.optionCount) {
                JsonArray opts = o["options"].to<JsonArray>();
                for (uint8_t n = 0; n < it.optionCount; ++n) opts.add(it.options[n]);
            }
        }

        JsonArray secrets = root["secrets"].to<JsonArray>();
        for (uint8_t i = 0; i < kSecretCount; ++i) {
            JsonObject o = secrets.add<JsonObject>();
            o["key"] = kSecretSchema[i].key;
            o["label"] = kSecretSchema[i].label;
            o["masked"] = kSecretSchema[i].masked;
        }

        writeJson(doc, res, buffer, bufferSize);
        return WebAction::None;
    }

    // --- Anzeigeeinstellungen ---------------------------------------------
    if (isPath(req, "/api/config")) {
        if (!config_) {
            writeText(res, buffer, bufferSize, 500, "keine Konfiguration");
            return WebAction::None;
        }

        if (isPost(req)) {
            JsonDocument in;
            if (deserializeJson(in, req.body)) {
                writeText(res, buffer, bufferSize, 400, "JSON kaputt");
                return WebAction::None;
            }
            uint8_t applied = 0, clamped = 0, unknown = 0;
            for (JsonPairConst kv : in.as<JsonObjectConst>()) {
                ConfigKey k;
                if (!Config::keyIndex(kv.key().c_str(), k) || !kv.value().is<int32_t>()) {
                    ++unknown;
                    continue;
                }
                if (!config_->set(k, kv.value().as<int32_t>())) ++clamped;
                ++applied;
            }
            JsonDocument out;
            out["applied"] = applied;
            out["clamped"] = clamped;
            out["unknown"] = unknown;
            writeJson(out, res, buffer, bufferSize);
            return WebAction::None;
        }

        JsonDocument doc;
        toJson(*config_, doc.to<JsonObject>());
        writeJson(doc, res, buffer, bufferSize);
        return WebAction::None;
    }

    // --- Zugangsdaten ------------------------------------------------------
    if (isPath(req, "/api/secrets")) {
        if (!secrets_) {
            writeText(res, buffer, bufferSize, 500, "keine Zugangsdaten");
            return WebAction::None;
        }

        if (isPost(req)) {
            JsonDocument in;
            if (deserializeJson(in, req.body)) {
                writeText(res, buffer, bufferSize, 400, "JSON kaputt");
                return WebAction::None;
            }
            uint8_t applied = 0, truncated = 0, unknown = 0;
            bool wifiTouched = false, mqttTouched = false;

            for (JsonPairConst kv : in.as<JsonObjectConst>()) {
                SecretKey k;
                if (!Secrets::keyIndex(kv.key().c_str(), k) || !kv.value().is<const char*>()) {
                    ++unknown;
                    continue;
                }
                if (!secrets_->set(k, kv.value().as<const char*>())) ++truncated;
                ++applied;
                if (k == SecretKey::WifiSsid || k == SecretKey::WifiPass) wifiTouched = true;
                else mqttTouched = true;
            }

            JsonDocument out;
            out["applied"] = applied;
            out["truncated"] = truncated;
            out["unknown"] = unknown;
            writeJson(out, res, buffer, bufferSize);

            // Neu verbinden, damit frische Zugangsdaten sofort greifen -- sonst
            // muesste man raten, ob sie stimmen.
            if (wifiTouched) return WebAction::ReconnectWifi;
            if (mqttTouched) return WebAction::ReconnectMqtt;
            return WebAction::None;
        }

        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        for (uint8_t i = 0; i < kSecretCount; ++i)
            o[kSecretSchema[i].key] = secrets_->masked(SecretKey(i));
        writeJson(doc, res, buffer, bufferSize);
        return WebAction::None;
    }

    // --- Zustand -----------------------------------------------------------
    if (isPath(req, "/api/status")) {
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        char timeBuf[8];
        std::snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", status_.hours, status_.minutes);
        o["time"] = timeBuf;

        static const char* kStateNames[] = {"tag", "nacht", "aus"};
        o["display"] = kStateNames[uint8_t(status_.displayState)];
        o["fault"] = faultName(status_.health.fault);
        o["severity"] = status_.health.severity == Severity::Ok
                            ? "ok"
                            : (status_.health.severity == Severity::Warning ? "warnung"
                                                                            : "kritisch");
        o["code"] = status_.health.code;
        o["wifi"] = status_.wifiConnected;
        o["ap"] = status_.apActive;
        o["rssi"] = status_.rssi;
        o["mqtt_enabled"] = status_.mqttEnabled;
        o["mqtt"] = status_.mqttConnected;
        o["heap"] = status_.heap;
        o["uptime"] = status_.uptimeS;
        o["ip"] = status_.ip;
        o["host"] = status_.hostname;

        writeJson(doc, res, buffer, bufferSize);
        return WebAction::None;
    }

    // --- Aktionen ----------------------------------------------------------
    if (isPath(req, "/api/restart") && isPost(req)) {
        writeText(res, buffer, bufferSize, 200, "Neustart");
        return WebAction::Restart;
    }

    if (isPath(req, "/api/factory-reset") && isPost(req)) {
        // Bewusst nur mit ausdruecklicher Bestaetigung im Rumpf: ein
        // versehentlicher POST soll nicht die Einrichtung loeschen.
        JsonDocument in;
        if (deserializeJson(in, req.body) || !in["confirm"].as<bool>()) {
            writeText(res, buffer, bufferSize, 400, "Bestaetigung fehlt");
            return WebAction::None;
        }
        writeText(res, buffer, bufferSize, 200, "Werksreset");
        return WebAction::FactoryReset;
    }

    writeText(res, buffer, bufferSize, 404, "Nicht gefunden");
    return WebAction::None;
}

}  // namespace wordclock
