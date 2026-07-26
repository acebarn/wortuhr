#include "wordclock/Profiles.h"

namespace wordclock {

ClockStyle clockStyleFor(const Config& cfg, DisplayState state) {
    ClockStyle s;

    const bool night = (state == DisplayState::Night);

    s.color = night ? cfg.getColor(ConfigKey::NightColor) : cfg.getColor(ConfigKey::Color);
    s.gradientTo = cfg.getColor(ConfigKey::GradientTo);

    // Nachts ist Ruhe: kein Verlauf, keine Geisterwoerter, kein Atmen, harte
    // Uebergaenge. Das Profil soll nicht blenden und nichts einfangen.
    s.gradient = !night && cfg.getBool(ConfigKey::Gradient);
    s.ghost = night ? 0 : cfg.getU8(ConfigKey::Ghost);
    s.ghostFillers = cfg.getBool(ConfigKey::GhostFillers);
    s.breathDepth = night ? 0 : cfg.getU8(ConfigKey::BreathDepth);

    s.transition = night ? Transition::None
                         : Transition(cfg.getU8(ConfigKey::Transition));
    s.transitionMs = cfg.getU16(ConfigKey::TransitionMs);

    return s;
}

DotStyle dotStyleFor(const Config& cfg, DisplayState state) {
    DotStyle d;
    d.clockColor = (state == DisplayState::Night) ? cfg.getColor(ConfigKey::NightColor)
                                                  : cfg.getColor(ConfigKey::Color);
    return d;
}

uint8_t brightnessFor(const Config& cfg, DisplayState state) {
    // Im Aus-Zustand bleibt die Helligkeit ausdruecklich normal.
    //
    // Dunkel wird das Panel dadurch, dass niemand darauf zeichnet -- nicht
    // durch Herunterregeln. Wuerde hier 0 stehen, waere auch ein kritischer
    // Fehler unsichtbar, der den Aus-Zustand gerade durchbrechen soll
    // (DESIGN 6). Er blinkt dann wie im Normalbetrieb.
    if (state == DisplayState::Night) return cfg.getU8(ConfigKey::NightBrightness);
    return cfg.getU8(ConfigKey::Brightness);
}

}  // namespace wordclock
