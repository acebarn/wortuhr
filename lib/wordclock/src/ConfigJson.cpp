#include "wordclock/ConfigJson.h"

namespace wordclock {
namespace {

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

}  // namespace

void toJson(const Config& cfg, JsonObject out) {
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        out[kSchema[i].key] = cfg.get(ConfigKey(i));
    }
}

LoadReport fromJson(Config& cfg, JsonObjectConst in) {
    LoadReport rep;

    for (JsonPairConst kv : in) {
        ConfigKey k;
        if (!Config::keyIndex(kv.key().c_str(), k)) {
            ++rep.unknown;
            continue;
        }
        if (!kv.value().is<int32_t>()) {
            ++rep.unknown;
            continue;
        }
        const int32_t raw = kv.value().as<int32_t>();
        if (!cfg.set(k, raw)) ++rep.clamped;
        ++rep.applied;
    }

    rep.missing = uint8_t(kConfigCount - rep.applied);
    return rep;
}

void schemaToJson(JsonArray out) {
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const ConfigItem& it = kSchema[i];
        JsonObject o = out.add<JsonObject>();
        o["key"] = it.key;
        o["label"] = it.label;
        o["category"] = it.category;
        o["type"] = typeName(it.type);
        o["min"] = it.min;
        o["max"] = it.max;
        o["default"] = it.def;
        if (it.options && it.optionCount) {
            JsonArray opts = o["options"].to<JsonArray>();
            for (uint8_t n = 0; n < it.optionCount; ++n) opts.add(it.options[n]);
        }
    }
}

}  // namespace wordclock
