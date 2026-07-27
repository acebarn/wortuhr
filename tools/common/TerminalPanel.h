#pragma once

// Panelausgabe im Terminal.
//
// Von Vorschau und Simulator gemeinsam benutzt: zwei Darstellungen desselben
// Panels wuerden frueher oder spaeter auseinanderlaufen.
//
// Alles inline -- der Header wird von mehreren Uebersetzungseinheiten
// eingebunden.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "wordclock/Frame.h"
#include "wordclock/WordLayout.h"

namespace termpanel {

using wordclock::Frame;
using wordclock::kBlack;
using wordclock::kGrid;
using wordclock::kHeight;
using wordclock::kWidth;
using wordclock::Rgb;

// Zeilen, die printPanel ausgibt -- fuer cursorUp beim Animieren.
inline constexpr int kPrintedLines = 14;

inline bool& truecolor() {
    static bool value = false;
    return value;
}

inline void detectColorSupport() {
    const char* ct = std::getenv("COLORTERM");
    truecolor() = ct && (std::strstr(ct, "truecolor") || std::strstr(ct, "24bit"));
}

// 256-Farben-Wuerfel als Rueckfall -- Terminal.app auf macOS kann kein Truecolor.
inline int cube256(Rgb c) {
    const int r = c.r * 5 / 255, g = c.g * 5 / 255, b = c.b * 5 / 255;
    return 16 + 36 * r + 6 * g + b;
}

inline std::string fg(Rgb c) {
    char buf[32];
    if (truecolor())
        std::snprintf(buf, sizeof(buf), "\033[38;2;%u;%u;%um", c.r, c.g, c.b);
    else
        std::snprintf(buf, sizeof(buf), "\033[38;5;%dm", cube256(c));
    return buf;
}

inline constexpr const char* kReset = "\033[0m";

// Unbeleuchtete LEDs sind auf dem Panel wirklich schwarz. Hier werden sie
// trotzdem schwach angedeutet, damit das Raster lesbar bleibt -- aber deutlich
// dunkler als das schwaechste sinnvolle Geisterwort, sonst waere ausgerechnet
// der Geistereffekt nicht zu beurteilen.
inline constexpr Rgb kUnlit{13, 13, 15};

inline void printPanel(const Frame& f, const char* status) {
    // Eckenanordnung am Geraet ausgemessen: 0 = oben rechts, 1 = oben links,
    // 2 = unten links, 3 = unten rechts (gegen den Uhrzeigersinn).
    auto dot = [&](uint8_t d) {
        const Rgb c = f.dot(d);
        return fg(c == kBlack ? kUnlit : c) + "●" + kReset;
    };

    std::printf("\n  %s                       %s\n", dot(1).c_str(), dot(0).c_str());

    for (uint8_t y = 0; y < kHeight; ++y) {
        std::printf("    ");
        for (uint8_t x = 0; x < kWidth; ++x) {
            const Rgb c = f.xy(x, y);
            const uint16_t cell = uint16_t(y) * kWidth + x;
            std::printf("%s%c%s ", fg(c == kBlack ? kUnlit : c).c_str(), kGrid[cell], kReset);
        }
        std::printf("\n");
    }

    std::printf("  %s                       %s\n", dot(2).c_str(), dot(3).c_str());
    std::printf("\n  \033[2m%s\033[0m\033[K\n", status);
}

inline void cursorUp(int lines) { std::printf("\033[%dA", lines); }

}  // namespace termpanel
