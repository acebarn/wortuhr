#include "wordclock/Compositor.h"

namespace wordclock {

uint32_t estimateCurrentMa(const Frame& f) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i < kLedCount; ++i) {
        const Rgb c = f.stripAt(i);
        sum += uint32_t(c.r) + c.g + c.b;
    }
    return sum * kMaPerChannelFull / 255;
}

void Compositor::buildTarget(const Frame& base, const Overlay& overlay, const Modifiers& mod) {
    target_ = base;
    overlay.blendInto(target_);

    // tint und modulation nur auf dem Buchstabenfeld -- die Eckpunkte gehoeren
    // dem Gesundheitskanal und duerfen von Darstellung und Benachrichtigungen
    // nicht angefasst werden.
    if (mod.tint != kWhite || mod.modulation != 255) {
        for (uint16_t c = 0; c < kLetterCount; ++c) {
            Rgb v = target_.cell(c);
            if (mod.tint != kWhite) v = modulate(v, mod.tint);
            if (mod.modulation != 255) v = scale(v, mod.modulation);
            target_.setCell(c, v);
        }
    }

    // Globale Helligkeit wirkt auf alles, Eckpunkte eingeschlossen.
    if (mod.brightness != 255) {
        for (uint16_t i = 0; i < kLetterCount; ++i)
            target_.setCell(i, scale(target_.cell(i), mod.brightness));
        for (uint8_t d = 0; d < kDotCount; ++d)
            target_.setDot(d, scale(target_.dot(d), mod.brightness));
    }

    // Strombegrenzung zuletzt, damit sie die tatsaechlich ausgegebenen Werte
    // sieht. Sie greift proportional auf allem, sonst verschoeben sich die
    // Farbverhaeltnisse.
    lastLimitScale_ = 255;
    const uint32_t ma = estimateCurrentMa(target_);
    if (currentLimitMa_ > 0 && ma > currentLimitMa_) {
        lastLimitScale_ = uint8_t(uint32_t(currentLimitMa_) * 255 / ma);
        for (uint16_t i = 0; i < kLetterCount; ++i)
            target_.setCell(i, scale(target_.cell(i), lastLimitScale_));
        for (uint8_t d = 0; d < kDotCount; ++d)
            target_.setDot(d, scale(target_.dot(d), lastLimitScale_));
    }
}

const Frame& Compositor::step(const Frame& base, const Overlay& overlay, const Modifiers& mod) {
    buildTarget(base, overlay, mod);

    for (uint16_t c = 0; c < kLetterCount; ++c)
        current_.setCell(c, lerp(current_.cell(c), target_.cell(c), smoothing_));
    for (uint8_t d = 0; d < kDotCount; ++d)
        current_.setDot(d, lerp(current_.dot(d), target_.dot(d), smoothing_));

    return current_;
}

const Frame& Compositor::snap(const Frame& base, const Overlay& overlay, const Modifiers& mod) {
    buildTarget(base, overlay, mod);
    current_ = target_;
    return current_;
}

}  // namespace wordclock
