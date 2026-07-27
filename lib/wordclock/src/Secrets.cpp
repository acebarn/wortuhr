#include "wordclock/Secrets.h"

#include <cstdlib>
#include <cstring>

namespace wordclock {

void Secrets::reset() {
    for (uint8_t i = 0; i < kSecretCount; ++i) {
        std::strncpy(values_[i], kSecretSchema[i].def, kSecretMaxLen - 1);
        values_[i][kSecretMaxLen - 1] = '\0';
    }
    dirty_ = false;
}

bool Secrets::set(SecretKey k, const char* value) {
    if (uint8_t(k) >= kSecretCount || !value) return false;

    // Der Platzhalter bedeutet "unveraendert lassen". Sonst wuerde ein
    // Speichern der Webseite jedes Kennwort durch seine eigene Maskierung
    // ersetzen -- und niemand kaeme mehr ins WLAN.
    if (kSecretSchema[uint8_t(k)].masked && !std::strcmp(value, kMaskPlaceholder)) return true;

    char* dst = values_[uint8_t(k)];
    const bool fits = std::strlen(value) < kSecretMaxLen;

    if (std::strcmp(dst, value) != 0) {
        std::strncpy(dst, value, kSecretMaxLen - 1);
        dst[kSecretMaxLen - 1] = '\0';
        dirty_ = true;
        ++revision_;
    }
    return fits;
}

bool Secrets::keyIndex(const char* key, SecretKey& out) {
    if (!key) return false;
    for (uint8_t i = 0; i < kSecretCount; ++i) {
        if (!std::strcmp(kSecretSchema[i].key, key)) {
            out = SecretKey(i);
            return true;
        }
    }
    return false;
}

bool Secrets::setByName(const char* key, const char* value) {
    SecretKey k;
    if (!keyIndex(key, k)) return false;
    return set(k, value);
}

const char* Secrets::masked(SecretKey k) const {
    if (uint8_t(k) >= kSecretCount) return "";
    if (!kSecretSchema[uint8_t(k)].masked) return values_[uint8_t(k)];
    return isSet(k) ? kMaskPlaceholder : "";
}

uint16_t Secrets::mqttPort() const {
    const long p = std::strtol(values_[uint8_t(SecretKey::MqttPort)], nullptr, 10);
    return (p > 0 && p < 65536) ? uint16_t(p) : 1883;
}

}  // namespace wordclock
