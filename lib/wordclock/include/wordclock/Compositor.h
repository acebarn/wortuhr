#pragma once

#include <cstdint>

#include "wordclock/Frame.h"

namespace wordclock {

// Schaetzung: 20 mA je Farbkanal bei Vollausschlag, also 60 mA fuer eine
// weisse LED. Uebernommen aus der Altfirmware, dort bewaehrt.
inline constexpr uint16_t kMaPerChannelFull = 20;
inline constexpr uint16_t kDefaultCurrentLimitMa = 2500;

// Geschaetzte Gesamtstromaufnahme des Panels bei den tatsaechlichen Pixelwerten.
uint32_t estimateCurrentMa(const Frame& f);

struct Modifiers {
    // Umfaerbung des Wortfelds durch eine Benachrichtigung. Ersetzt den
    // Farbton und behaelt die Helligkeit je Pixel (siehe recolorTo).
    //
    // Wirkt NUR auf das Buchstabenfeld: Benachrichtigungen duerfen die
    // Eckpunkte nicht erreichen, sonst bricht der Vertrag "Bewegung heisst
    // Stoerung" (DESIGN 4).
    bool recolor = false;
    Rgb tintColor = kWhite;

    // Zeitabhaengige Helligkeitsmodulation fuer pulse/blink. Ebenfalls nur
    // auf dem Buchstabenfeld.
    uint8_t modulation = 255;

    // Globale Helligkeit aus dem Darstellungsprofil. Wirkt auf alles,
    // Eckpunkte eingeschlossen.
    uint8_t brightness = 255;
};

// Setzt die Ebenen zum Zielbild zusammen und fuehrt das aktuelle Bild weich
// dorthin nach.
//
//   Basis  +  Overlay  +  Modifier  =  target
//                                        | lerp(smoothing)
//                                      current  ->  Strip
//
// Das weiche Nachfuehren ist der Grund, warum der Minutenwechsel gut aussieht;
// es wird aus der Altfirmware uebernommen.
class Compositor {
public:
    void setSmoothing(uint8_t f) { smoothing_ = f; }
    uint8_t smoothing() const { return smoothing_; }

    void setCurrentLimit(uint16_t ma) { currentLimitMa_ = ma; }

    // Ein Schritt: zusammensetzen, begrenzen, weich nachfuehren.
    const Frame& step(const Frame& base, const Overlay& overlay, const Modifiers& mod);

    // Ohne Ueberblendung sofort uebernehmen (Start, harte Uebergaenge).
    const Frame& snap(const Frame& base, const Overlay& overlay, const Modifiers& mod);

    const Frame& current() const { return current_; }
    const Frame& target() const { return target_; }

    // Letzte Strombegrenzung: 255 = nicht eingegriffen.
    uint8_t lastLimitScale() const { return lastLimitScale_; }

private:
    void buildTarget(const Frame& base, const Overlay& overlay, const Modifiers& mod);

    Frame target_;
    Frame current_;
    uint8_t smoothing_ = 128;  // ~0.5, entspricht der Altfirmware
    uint16_t currentLimitMa_ = kDefaultCurrentLimitMa;
    uint8_t lastLimitScale_ = 255;
};

}  // namespace wordclock
