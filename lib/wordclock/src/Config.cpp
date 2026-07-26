#include "wordclock/Config.h"

#include <cstring>

namespace wordclock {

void Config::reset() {
    for (uint8_t i = 0; i < kConfigCount; ++i) values_[i] = kSchema[i].def;
    dirty_ = false;
}

Rgb Config::getColor(ConfigKey k) const {
    const uint32_t v = uint32_t(values_[uint8_t(k)]);
    return {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
}

int32_t Config::clampTo(const ConfigItem& item, int32_t value) {
    if (value < item.min) return item.min;
    if (value > item.max) return item.max;
    return value;
}

bool Config::set(ConfigKey k, int32_t value) {
    if (uint8_t(k) >= kConfigCount) return false;
    const ConfigItem& item = kSchema[uint8_t(k)];
    const int32_t clamped = clampTo(item, value);
    if (values_[uint8_t(k)] != clamped) {
        values_[uint8_t(k)] = clamped;
        dirty_ = true;
        ++revision_;
    }
    return clamped == value;
}

bool Config::keyIndex(const char* key, ConfigKey& out) {
    if (!key) return false;
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        if (std::strcmp(kSchema[i].key, key) == 0) {
            out = ConfigKey(i);
            return true;
        }
    }
    return false;
}

bool Config::setByName(const char* key, int32_t value) {
    ConfigKey k;
    if (!keyIndex(key, k)) return false;
    return set(k, value);
}

// --- Darstellungszustand ---------------------------------------------------

bool inWindow(uint16_t minute, uint16_t from, uint16_t to) {
    if (from == to) return false;             // leeres Fenster
    if (from < to) return minute >= from && minute < to;
    return minute >= from || minute < to;     // laeuft ueber Mitternacht
}

DisplayState displayStateFor(const Config& cfg, uint8_t hours, uint8_t minutes) {
    const uint16_t now = uint16_t(hours) * 60 + minutes;

    // Aus schlaegt Nacht: der Grund fuer das Aus-Fenster ist ein schlafender
    // Gast, und der ist wichtiger als eine gedimmte Anzeige.
    if (cfg.getBool(ConfigKey::OffEnabled) &&
        inWindow(now, uint16_t(cfg.get(ConfigKey::OffFrom)),
                 uint16_t(cfg.get(ConfigKey::OffTo)))) {
        return DisplayState::Off;
    }

    if (cfg.getBool(ConfigKey::NightEnabled) &&
        inWindow(now, uint16_t(cfg.get(ConfigKey::NightFrom)),
                 uint16_t(cfg.get(ConfigKey::NightTo)))) {
        return DisplayState::Night;
    }

    return DisplayState::Day;
}

}  // namespace wordclock
