#pragma once

#include <cstdint>

#include "wordclock/Frame.h"
#include "wordclock/TimeToWords.h"

namespace wordclock {

// Uebergangsstil beim Minutenwechsel. Genau einer ist aktiv -- die drei
// bewegten Varianten beanspruchen denselben Moment und wuerden sich sonst
// gegenseitig unkenntlich machen (DESIGN 7.1, Rolle 1).
enum class Transition : uint8_t {
    None,       // hart umschalten
    Staggered,  // Woerter nacheinander in Leserichtung
    FadeTop,    // zeilenweise von oben nach unten
    Falling,    // Buchstaben fallen von oben in ihre Position
};

struct ClockStyle {
    // Rolle 2 -- statische Modifikatoren, bewegen sich nie
    Rgb color{255, 180, 60};
    Rgb gradientTo{255, 90, 20};
    bool gradient = false;
    uint8_t ghost = 0;          // Grundhelligkeit unbenutzter Woerter, 0 = aus
    bool ghostFillers = false;  // auch die 15 Fuellbuchstaben glimmen lassen

    // Rolle 1 -- Uebergang
    Transition transition = Transition::Staggered;
    uint16_t transitionMs = 400;

    // Rolle 3 -- Dauerbewegung
    uint8_t breathDepth = 0;  // Tiefe des Sekundenatmens, 0 = aus
    uint16_t breathPeriodMs = 1000;
};

// Zeichnet die Uhrzeit als Basis-Ebene.
//
// Zeichnet AUSSCHLIESSLICH das Buchstabenfeld. Die vier Eckpunkte gehoeren dem
// Gesundheitskanal und werden an anderer Stelle vergeben -- auch die
// Minutenanzeige, weil ueber sie erst der Gesundheitszustand entscheidet
// (DESIGN 4). Der Renderer fasst sie nicht an.
class ClockRenderer {
public:
    void setStyle(const ClockStyle& s) { style_ = s; }
    const ClockStyle& style() const { return style_; }

    // nowMs ist eine monotone Millisekundenzeit (millis()), unabhaengig von
    // der Uhrzeit -- Uebergaenge und Atmen duerfen nicht springen, wenn NTP
    // die Uhr nachstellt.
    void render(Frame& out, uint8_t hours, uint8_t minutes, uint32_t nowMs);

    // Naechstes render() ohne Uebergang darstellen.
    void reset() { hasLast_ = false; }

    // 0..255, nur fuer Tests und Diagnose.
    uint8_t transitionProgress(uint32_t nowMs) const;

private:
    Rgb colorAt(uint8_t index, uint8_t total) const;
    uint8_t breathScale(uint32_t nowMs) const;

    ClockStyle style_;
    Sentence last_;
    bool hasLast_ = false;
    uint32_t transitionStart_ = 0;
};

}  // namespace wordclock
