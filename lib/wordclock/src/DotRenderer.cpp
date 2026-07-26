#include "wordclock/DotRenderer.h"

namespace wordclock {

uint8_t DotRenderer::dotCount(const HealthState& state, uint8_t minutes) const {
    // Beim Lauflicht sind immer alle vier beteiligt -- der laufende ist hell,
    // die uebrigen glimmen mit.
    if (state.rhythm == Rhythm::Chase) return kDotCount;

    switch (state.severity) {
        case Severity::Ok:
            return minuteDots(minutes);

        case Severity::Warning: {
            // Die Anzahl bleibt die Minute, damit eine Warnung keine
            // Information kostet. Aber bei Minute 0, 5, 10 ... waere sie
            // unsichtbar, weil dann kein Punkt leuchtet -- ein Fuenftel der
            // Zeit waere die Uhr warnblind. Deshalb hier mindestens einer.
            const uint8_t n = minuteDots(minutes);
            return n ? n : uint8_t(1);
        }

        case Severity::Critical:
            return state.code;
    }
    return 0;
}

Rgb DotRenderer::colorFor(const HealthState& state) const {
    switch (state.severity) {
        case Severity::Ok: return style_.clockColor;
        case Severity::Warning: return style_.warningColor;
        case Severity::Critical:
            return state.fault == Fault::ApMode ? style_.awaitingColor : style_.criticalColor;
    }
    return style_.clockColor;
}

uint8_t DotRenderer::scaleFor(Rhythm r, uint8_t dot, uint32_t nowMs) const {
    switch (r) {
        case Rhythm::Steady:
            return 255;

        case Rhythm::Breathing: {
            const uint8_t eased = breathCurve(phaseOf(nowMs, style_.breathPeriodMs));
            return uint8_t(style_.breathFloor +
                           (uint16_t(255 - style_.breathFloor) * eased) / 255);
        }

        case Rhythm::Blinking: {
            const uint32_t half = style_.blinkPeriodMs / 2;
            if (half == 0) return 255;
            return ((nowMs / half) % 2) ? 0 : 255;
        }

        case Rhythm::Chase: {
            const uint32_t stepMs = style_.chaseStepMs ? style_.chaseStepMs : 1;
            const uint8_t active = uint8_t((nowMs / stepMs) % kDotCount);
            return dot == active ? 255 : style_.chaseFloor;
        }
    }
    return 255;
}

void DotRenderer::render(Frame& out, const HealthState& state, uint8_t minutes, uint32_t nowMs,
                         bool panelOff) const {
    // Nur die Punkte leeren -- das Wortfeld gehoert dem ClockRenderer.
    out.clearDots();

    // Im Aus-Zustand schweigt der Kanal, ausser es ist wirklich ernst.
    if (panelOff && state.severity != Severity::Critical) return;

    const uint8_t count = dotCount(state, minutes);
    const Rgb color = colorFor(state);

    for (uint8_t d = 0; d < count && d < kDotCount; ++d) {
        out.setDot(d, scale(color, scaleFor(state.rhythm, d, nowMs)));
    }
}

}  // namespace wordclock
