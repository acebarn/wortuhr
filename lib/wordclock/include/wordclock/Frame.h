#pragma once

#include <cstdint>

#include "wordclock/Color.h"
#include "wordclock/Geometry.h"

namespace wordclock {

// Ein vollstaendiges Bild des Panels.
//
// Intern in Strip-Reihenfolge gespeichert, nach aussen aber ausschliesslich
// ueber Rasterkoordinaten und Punktnummern ansprechbar. Das ist die einzige
// Stelle, an der die Serpentine-Abbildung angewendet wird -- Ebenen,
// Animationen und Adapter sehen nie einen LED-Index.
//
// Buchstabenraster und Minutenpunkte sind getrennte Zugriffswege. Ihre
// Verwechslung war die Fehlerursache im Altprojekt.
class Frame {
public:
    void clear() { fillAll(kBlack); }

    void fillAll(Rgb c) {
        for (uint16_t i = 0; i < kLedCount; ++i) px_[i] = c;
    }

    // Nur das Buchstabenraster, Minutenpunkte bleiben unberuehrt.
    void fillLetters(Rgb c) {
        for (uint16_t i = 0; i < kLetterCount; ++i) px_[letterIndexFromCell(i)] = c;
    }

    // --- Buchstabenraster ---------------------------------------------------

    // cell = Position in Leserichtung, 0 .. kLetterCount-1
    void setCell(uint16_t cell, Rgb c) {
        if (cell < kLetterCount) px_[letterIndexFromCell(cell)] = c;
    }

    Rgb cell(uint16_t cell) const {
        return cell < kLetterCount ? px_[letterIndexFromCell(cell)] : kBlack;
    }

    void setXY(uint8_t x, uint8_t y, Rgb c) {
        if (inBounds(x, y)) px_[letterIndex(x, y)] = c;
    }

    Rgb xy(uint8_t x, uint8_t y) const { return inBounds(x, y) ? px_[letterIndex(x, y)] : kBlack; }

    // --- Minutenpunkte ------------------------------------------------------

    void setDot(uint8_t dot, Rgb c) {
        if (dot < kDotCount) px_[dotIndex(dot)] = c;
    }

    Rgb dot(uint8_t dot) const { return dot < kDotCount ? px_[dotIndex(dot)] : kBlack; }

    void clearDots() {
        for (uint8_t i = 0; i < kDotCount; ++i) px_[dotIndex(i)] = kBlack;
    }

    // --- Ausgabe ------------------------------------------------------------

    // Strip-Reihenfolge. Nur fuer den LED-Adapter.
    const Rgb* strip() const { return px_; }
    Rgb stripAt(uint16_t i) const { return i < kLedCount ? px_[i] : kBlack; }

private:
    Rgb px_[kLedCount] = {};
};

// Eine Ebene, die das darunterliegende Bild teilweise verdeckt.
//
// Deckung je LED, damit `glyph` und `word` nur dort wirken, wo sie etwas
// zeichnen, und den Rest der Uhrzeit stehen lassen.
class Overlay {
public:
    void clear() {
        color_.clear();
        for (uint16_t i = 0; i < kLedCount; ++i) alpha_[i] = 0;
    }

    void setCell(uint16_t cell, Rgb c, uint8_t alpha = 255) {
        if (cell >= kLetterCount) return;
        color_.setCell(cell, c);
        alpha_[letterIndexFromCell(cell)] = alpha;
    }

    void setXY(uint8_t x, uint8_t y, Rgb c, uint8_t alpha = 255) {
        if (!inBounds(x, y)) return;
        color_.setXY(x, y, c);
        alpha_[letterIndex(x, y)] = alpha;
    }

    void setDot(uint8_t dot, Rgb c, uint8_t alpha = 255) {
        if (dot >= kDotCount) return;
        color_.setDot(dot, c);
        alpha_[dotIndex(dot)] = alpha;
    }

    uint8_t alphaAt(uint16_t stripIdx) const { return stripIdx < kLedCount ? alpha_[stripIdx] : 0; }
    const Frame& colors() const { return color_; }

    // Ueber ein bestehendes Bild legen.
    void blendInto(Frame& dst) const;

private:
    Frame color_;
    uint8_t alpha_[kLedCount] = {};
};

}  // namespace wordclock
