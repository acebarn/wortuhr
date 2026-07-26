#include "wordclock/ClockRenderer.h"

namespace wordclock {
namespace {

constexpr uint8_t clamp255(int32_t v) {
    return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Einblendkurve fuer die versetzten Uebergaenge.
//
// `slot` ist die Position des Elements in der Staffelung (Wort- oder
// Zeilennummer), `slots` deren Anzahl. Jedes Element startet spaeter und
// blendet dann ueber ein festes Fenster ein.
constexpr uint8_t kFadeWindow = 128;  // von 255

uint8_t staggeredAlpha(uint8_t progress, uint8_t slot, uint8_t slots) {
    if (progress >= 255) return 255;  // Endzustand ist immer vollstaendig
    if (slots <= 1) return progress;

    const uint8_t step = uint8_t((255 - kFadeWindow) / (slots - 1));
    const int32_t local = int32_t(progress) - int32_t(slot) * step;
    if (local <= 0) return 0;
    return clamp255(local * 255 / kFadeWindow);
}

}  // namespace

uint8_t ClockRenderer::transitionProgress(uint32_t nowMs) const {
    if (!hasLast_ || style_.transition == Transition::None || style_.transitionMs == 0) return 255;
    const uint32_t dt = nowMs - transitionStart_;
    if (dt >= style_.transitionMs) return 255;
    return uint8_t(dt * 255 / style_.transitionMs);
}

Rgb ClockRenderer::colorAt(uint8_t index, uint8_t total) const {
    if (!style_.gradient || total <= 1) return style_.color;
    return lerp(style_.color, style_.gradientTo, uint8_t(uint16_t(index) * 255 / (total - 1)));
}

uint8_t ClockRenderer::breathScale(uint32_t nowMs) const {
    if (style_.breathDepth == 0) return 255;
    const uint32_t period = style_.breathPeriodMs ? style_.breathPeriodMs : 1000;
    const uint8_t eased = breathCurve(phaseOf(nowMs, period));
    return uint8_t(255 - style_.breathDepth + (uint16_t(style_.breathDepth) * eased) / 255);
}

void ClockRenderer::render(Frame& out, uint8_t hours, uint8_t minutes, uint32_t nowMs) {
    const Sentence s = timeToWords(hours, minutes);

    if (!hasLast_) {
        last_ = s;
        hasLast_ = true;
        transitionStart_ = nowMs - style_.transitionMs;  // erster Frame ohne Uebergang
    } else if (s != last_) {
        last_ = s;
        transitionStart_ = nowMs;
    }

    // Nur das Buchstabenfeld leeren -- die Eckpunkte gehoeren uns nicht.
    out.fillLetters(kBlack);

    // Geisterwoerter zuerst, sie liegen unter der aktiven Kette und atmen nicht
    // mit: eine ruhige Grundflaeche stoert das Bewegungssignal nicht.
    if (style_.ghost > 0) {
        const Rgb g = scale(style_.color, style_.ghost);
        for (uint16_t c = 0; c < kLetterCount; ++c) {
            if (style_.ghostFillers || kWordCells.test(c)) out.setCell(c, g);
        }
    }

    // Aktive Kette einsammeln, in Leserichtung.
    uint16_t cells[kMaxSentenceLetters];
    uint8_t slotOf[kMaxSentenceLetters];  // Wortnummer, fuer Staggered
    uint8_t n = 0;
    for (uint8_t w = 0; w < s.len && n < kMaxSentenceLetters; ++w) {
        const WordSpan sp = span(s.words[w]);
        for (uint8_t i = 0; i < sp.len && n < kMaxSentenceLetters; ++i) {
            cells[n] = uint16_t(sp.cell + i);
            slotOf[n] = w;
            ++n;
        }
    }

    const uint8_t progress = transitionProgress(nowMs);
    const uint8_t breath = breathScale(nowMs);

    for (uint8_t i = 0; i < n; ++i) {
        const uint16_t cell = cells[i];
        const uint8_t x = uint8_t(cell % kWidth);
        const uint8_t y = uint8_t(cell / kWidth);
        const Rgb base = scale(colorAt(i, n), breath);

        switch (style_.transition) {
            case Transition::None:
                out.setCell(cell, base);
                break;

            case Transition::Staggered: {
                const uint8_t a = staggeredAlpha(progress, slotOf[i], s.len);
                if (a) out.setCell(cell, lerp(out.cell(cell), base, a));
                break;
            }

            case Transition::FadeTop: {
                const uint8_t a = staggeredAlpha(progress, y, kHeight);
                if (a) out.setCell(cell, lerp(out.cell(cell), base, a));
                break;
            }

            case Transition::Falling: {
                // Der Buchstabe faellt von oberhalb des Panels in seine Zeile.
                // Versetzt nach Spalte, damit es wie ein Regen von links nach
                // rechts wirkt statt wie ein gleichzeitiger Absturz.
                const uint8_t a = staggeredAlpha(progress, x, kWidth);
                if (a == 0) break;
                if (a >= 255) {
                    out.setCell(cell, base);
                    break;
                }
                // Position zwischen "eine Zeile ueber dem Panel" und Ziel.
                const int32_t travel = int32_t(y) + 1;
                const int32_t pos = int32_t(a) * travel / 255 - 1;
                if (pos >= 0) out.setXY(x, uint8_t(pos), base);
                break;
            }
        }
    }
}

}  // namespace wordclock
