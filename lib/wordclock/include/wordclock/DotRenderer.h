#pragma once

#include <cstdint>

#include "wordclock/Frame.h"
#include "wordclock/Health.h"
#include "wordclock/TimeToWords.h"

namespace wordclock {

struct DotStyle {
    // Im gesunden Zustand tragen die Punkte die Minute in der Uhrenfarbe.
    Rgb clockColor{255, 180, 60};
    Rgb warningColor{255, 130, 0};
    Rgb criticalColor{255, 0, 0};
    Rgb awaitingColor{0, 90, 255};

    uint16_t breathPeriodMs = 3000;
    uint8_t breathFloor = 90;  // Warnung wird nie ganz dunkel -- sonst waere
                               // sie vom Blinken nicht zu unterscheiden

    uint16_t blinkPeriodMs = 600;

    uint16_t chaseStepMs = 180;
    uint8_t chaseFloor = 30;  // nicht laufende Punkte glimmen mit, damit das
                              // Lauflicht als Gruppe erkennbar bleibt
};

// Zeichnet ausschliesslich die vier Eckpunkte.
//
// Sie sind der Gesundheitskanal und gehoeren niemandem sonst -- auch die
// Minutenanzeige laeuft hier, weil erst der Gesundheitszustand entscheidet, ob
// die Punkte ueberhaupt die Minute zeigen duerfen (DESIGN 4).
class DotRenderer {
public:
    void setStyle(const DotStyle& s) { style_ = s; }
    const DotStyle& style() const { return style_; }

    // panelOff: Aus-Zustand fuer Uebernachtungsgaeste. Dann bleiben die Punkte
    // dunkel -- ausser bei kritischen Fehlern, die durchbrechen und wie im
    // Normalbetrieb blinken (DESIGN 6).
    void render(Frame& out, const HealthState& state, uint8_t minutes, uint32_t nowMs,
                bool panelOff = false) const;

    // Wie viele Punkte der Zustand belegt. Auch fuer Diagnose und Tests.
    uint8_t dotCount(const HealthState& state, uint8_t minutes) const;

private:
    uint8_t scaleFor(Rhythm r, uint8_t dot, uint32_t nowMs) const;
    Rgb colorFor(const HealthState& state) const;

    DotStyle style_;
};

}  // namespace wordclock
