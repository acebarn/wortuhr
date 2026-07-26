#pragma once

#include <cstdint>

namespace wordclock {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;

    constexpr bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    constexpr bool operator!=(const Rgb& o) const { return !(*this == o); }
};

inline constexpr Rgb kBlack{0, 0, 0};
inline constexpr Rgb kWhite{255, 255, 255};

// Ganzzahlige Interpolation von a nach b, f = 0 (bleibt) .. 255 (sofort dort).
//
// Wichtig: Bei einem Unterschied von 1 und kleinem f wuerde die reine
// Multiplikation 0 ergeben und der Wert stuende fuer immer eine Stufe neben dem
// Ziel. Die Altfirmware hatte genau dieses Problem -- ihre Ueberblendung kam
// dem Ziel bis auf wenige Einheiten nahe und blieb dort haengen. Deshalb hier
// kaufmaennisch runden und mindestens eine Stufe in Zielrichtung gehen.
constexpr uint8_t lerp8(uint8_t a, uint8_t b, uint8_t f) {
    if (f == 0 || a == b) return a;
    if (f == 255) return b;

    const int16_t diff = int16_t(b) - int16_t(a);
    int16_t step = int16_t((diff * int16_t(f) + (diff > 0 ? 127 : -127)) / 255);
    if (step == 0) step = (diff > 0) ? 1 : -1;
    return uint8_t(int16_t(a) + step);
}

constexpr Rgb lerp(Rgb a, Rgb b, uint8_t f) {
    return {lerp8(a.r, b.r, f), lerp8(a.g, b.g, f), lerp8(a.b, b.b, f)};
}

// Multiplikative Skalierung, 255 = unveraendert.
constexpr uint8_t scale8(uint8_t v, uint8_t s) { return uint8_t((uint16_t(v) * s + 127) / 255); }

constexpr Rgb scale(Rgb c, uint8_t s) { return {scale8(c.r, s), scale8(c.g, s), scale8(c.b, s)}; }

// Kanalweise Multiplikation. Weiss ist neutral -- so wirkt `tint` als Faerbung,
// ohne die Helligkeitsverhaeltnisse innerhalb der Wortkette zu zerstoeren.
constexpr Rgb modulate(Rgb c, Rgb t) {
    return {scale8(c.r, t.r), scale8(c.g, t.g), scale8(c.b, t.b)};
}

}  // namespace wordclock
