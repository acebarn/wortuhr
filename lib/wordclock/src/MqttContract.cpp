#include "wordclock/MqttContract.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wordclock {
namespace {

// strcasecmp ist POSIX und weder in std:: noch auf dem ESP8266 verlaesslich.
bool equalsIgnoreCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
        const char cb = (*b >= 'A' && *b <= 'Z') ? char(*b + 32) : *b;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

bool hexNibble(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') { out = uint8_t(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = uint8_t(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { out = uint8_t(c - 'A' + 10); return true; }
    return false;
}

}  // namespace

// --- Topics ----------------------------------------------------------------

void topicAvailability(char* out, size_t n, const char* prefix) {
    std::snprintf(out, n, "%s/availability", prefix);
}
void topicState(char* out, size_t n, const char* prefix) {
    std::snprintf(out, n, "%s/state", prefix);
}
void topicDiag(char* out, size_t n, const char* prefix) {
    std::snprintf(out, n, "%s/diag", prefix);
}
void topicNotify(char* out, size_t n, const char* prefix) {
    std::snprintf(out, n, "%s/notify", prefix);
}
void topicSet(char* out, size_t n, const char* prefix, const char* key) {
    std::snprintf(out, n, "%s/set/%s", prefix, key);
}
void topicSetWildcard(char* out, size_t n, const char* prefix) {
    std::snprintf(out, n, "%s/set/+", prefix);
}
void topicDiscovery(char* out, size_t n, const char* component, const char* device,
                    const char* key) {
    std::snprintf(out, n, "%s/%s/%s/%s/config", kDiscoveryPrefix, component, device, key);
}

bool keyFromSetTopic(const char* topic, const char* prefix, char* out, size_t n) {
    if (!topic || !prefix || n == 0) return false;

    char head[96];
    std::snprintf(head, sizeof(head), "%s/set/", prefix);
    const size_t headLen = std::strlen(head);
    if (std::strncmp(topic, head, headLen) != 0) return false;

    const char* key = topic + headLen;
    if (!*key || std::strchr(key, '/')) return false;  // genau eine Ebene
    if (std::strlen(key) >= n) return false;

    std::strcpy(out, key);
    return true;
}

// --- Wertdarstellung -------------------------------------------------------

void formatValue(const ConfigItem& item, int32_t value, char* out, size_t n) {
    switch (item.type) {
        case ConfigType::Bool:
            std::snprintf(out, n, "%s", value ? "ON" : "OFF");
            break;
        case ConfigType::Color:
            std::snprintf(out, n, "%06lX", (unsigned long)(uint32_t(value) & 0xFFFFFFu));
            break;
        case ConfigType::TimeOfDay:
            std::snprintf(out, n, "%02d:%02d", int(value / 60), int(value % 60));
            break;
        case ConfigType::Choice:
            if (item.options && value >= 0 && value < item.optionCount)
                std::snprintf(out, n, "%s", item.options[value]);
            else
                std::snprintf(out, n, "%s", "?");
            break;
        case ConfigType::Number:
            std::snprintf(out, n, "%ld", (long)value);
            break;
    }
}

bool parseValue(const ConfigItem& item, const char* text, int32_t& out) {
    if (!text || !*text) return false;

    switch (item.type) {
        case ConfigType::Bool: {
            if (equalsIgnoreCase(text, "ON") || !std::strcmp(text, "1") ||
                equalsIgnoreCase(text, "true")) {
                out = 1;
                return true;
            }
            if (equalsIgnoreCase(text, "OFF") || !std::strcmp(text, "0") ||
                equalsIgnoreCase(text, "false")) {
                out = 0;
                return true;
            }
            return false;
        }

        case ConfigType::Color: {
            if (*text == '#') ++text;
            if (std::strlen(text) != 6) return false;
            uint32_t v = 0;
            for (uint8_t i = 0; i < 6; ++i) {
                uint8_t nib;
                if (!hexNibble(text[i], nib)) return false;
                v = (v << 4) | nib;
            }
            out = int32_t(v);
            return true;
        }

        case ConfigType::TimeOfDay: {
            int h = 0, m = 0;
            if (std::sscanf(text, "%d:%d", &h, &m) != 2) return false;
            if (h < 0 || h > 23 || m < 0 || m > 59) return false;
            out = h * 60 + m;
            return true;
        }

        case ConfigType::Choice: {
            if (!item.options) return false;
            for (uint8_t i = 0; i < item.optionCount; ++i) {
                if (!std::strcmp(text, item.options[i])) {
                    out = i;
                    return true;
                }
            }
            return false;
        }

        case ConfigType::Number: {
            char* end = nullptr;
            const long v = std::strtol(text, &end, 10);
            if (end == text) return false;
            // Nachkommastellen sind erlaubt und werden abgeschnitten -- HA
            // schickt fuer number-Entitaeten gern "90.0".
            if (*end && *end != '.') return false;
            out = int32_t(v);
            return true;
        }
    }
    return false;
}

// --- Discovery -------------------------------------------------------------

const char* componentFor(ConfigType t) {
    switch (t) {
        case ConfigType::Bool: return "switch";
        case ConfigType::Number: return "number";
        case ConfigType::Choice: return "select";
        // Fuer Farben und Uhrzeiten gibt es in der MQTT-Integration keinen
        // eigenen Entitaetstyp, auf den man sich verlassen koennte. `text`
        // ist seit Langem stabil und stellt beides lesbar dar: "FFB43C"
        // beziehungsweise "22:00".
        case ConfigType::Color: return "text";
        case ConfigType::TimeOfDay: return "text";
    }
    return "text";
}

namespace {

void addDevice(JsonObject out, const DeviceInfo& dev) {
    JsonObject d = out["dev"].to<JsonObject>();
    JsonArray ids = d["ids"].to<JsonArray>();
    ids.add(dev.id);
    d["name"] = dev.name;
    d["mf"] = dev.manufacturer;
    d["mdl"] = dev.model;
    if (dev.version && *dev.version) d["sw"] = dev.version;
}

void addAvailability(JsonObject out, const char* prefix) {
    char buf[80];
    topicAvailability(buf, sizeof(buf), prefix);
    out["avty_t"] = buf;
    out["pl_avail"] = kPayloadOnline;
    out["pl_not_avail"] = kPayloadOffline;
}

// Jinja-Ausdruck, der den rohen Zahlenwert aus dem State-JSON so aufbereitet,
// wie die Entitaet ihn erwartet.
void addValueTemplate(JsonObject out, const ConfigItem& item) {
    char tpl[240];
    switch (item.type) {
        case ConfigType::Bool:
            std::snprintf(tpl, sizeof(tpl), "{{ 'ON' if value_json.%s else 'OFF' }}", item.key);
            break;
        case ConfigType::Color:
            std::snprintf(tpl, sizeof(tpl), "{{ '%%06X'|format(value_json.%s) }}", item.key);
            break;
        case ConfigType::TimeOfDay:
            std::snprintf(tpl, sizeof(tpl),
                          "{{ '%%02d:%%02d'|format(value_json.%s // 60, value_json.%s %% 60) }}",
                          item.key, item.key);
            break;
        case ConfigType::Choice: {
            // Index -> Optionsname
            char list[160] = "[";
            for (uint8_t i = 0; i < item.optionCount; ++i) {
                std::strncat(list, "'", sizeof(list) - std::strlen(list) - 1);
                std::strncat(list, item.options[i], sizeof(list) - std::strlen(list) - 1);
                std::strncat(list, i + 1 < item.optionCount ? "'," : "'",
                             sizeof(list) - std::strlen(list) - 1);
            }
            std::strncat(list, "]", sizeof(list) - std::strlen(list) - 1);
            std::snprintf(tpl, sizeof(tpl), "{{ %s[value_json.%s] }}", list, item.key);
            break;
        }
        case ConfigType::Number:
            std::snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", item.key);
            break;
    }
    out["val_tpl"] = tpl;
}

}  // namespace

void buildConfigDiscovery(JsonObject out, const ConfigItem& item, const char* prefix,
                          const DeviceInfo& dev) {
    char buf[96];

    out["name"] = item.label;

    std::snprintf(buf, sizeof(buf), "%s_%s", dev.id, item.key);
    out["uniq_id"] = buf;
    out["obj_id"] = buf;

    topicState(buf, sizeof(buf), prefix);
    out["stat_t"] = buf;

    topicSet(buf, sizeof(buf), prefix, item.key);
    out["cmd_t"] = buf;

    addValueTemplate(out, item);
    addAvailability(out, prefix);
    addDevice(out, dev);

    switch (item.type) {
        case ConfigType::Number:
            out["min"] = item.min;
            out["max"] = item.max;
            out["step"] = 1;
            out["mode"] = "slider";
            break;
        case ConfigType::Choice: {
            JsonArray opts = out["ops"].to<JsonArray>();
            for (uint8_t i = 0; i < item.optionCount; ++i) opts.add(item.options[i]);
            break;
        }
        case ConfigType::Color:
            out["max"] = 7;  // "RRGGBB", eine Stelle Reserve fuer "#"
            break;
        case ConfigType::TimeOfDay:
            out["max"] = 5;  // "HH:MM"
            out["ptrn"] = "^([01]\\d|2[0-3]):[0-5]\\d$";
            break;
        case ConfigType::Bool:
            break;
    }

    // Einstellungen gehoeren in HA unter "Konfiguration", nicht zu den
    // Bedienelementen der Karte.
    out["ent_cat"] = "config";
}

void buildDiagDiscovery(JsonObject out, const DiagSensor& sensor, const char* prefix,
                        const DeviceInfo& dev) {
    char buf[96];

    out["name"] = sensor.label;

    std::snprintf(buf, sizeof(buf), "%s_%s", dev.id, sensor.key);
    out["uniq_id"] = buf;
    out["obj_id"] = buf;

    topicDiag(buf, sizeof(buf), prefix);
    out["stat_t"] = buf;

    std::snprintf(buf, sizeof(buf), "{{ value_json.%s }}", sensor.key);
    out["val_tpl"] = buf;

    if (sensor.unit && *sensor.unit) out["unit_of_meas"] = sensor.unit;
    if (sensor.deviceClass && *sensor.deviceClass) {
        out["dev_cla"] = sensor.deviceClass;
        out["stat_cla"] = "measurement";
    }

    addAvailability(out, prefix);
    addDevice(out, dev);
    out["ent_cat"] = "diagnostic";
}

}  // namespace wordclock
