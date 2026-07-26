#include "wordclock/Frame.h"

namespace wordclock {

void Overlay::blendInto(Frame& dst) const {
    // Bewusst ueber Rasterzellen und Punktnummern statt ueber Strip-Indizes:
    // so bleibt die Geometrie auch hier auf Frame beschraenkt. Die Schleifen
    // decken zusammen alle kLedCount LEDs ab.
    for (uint16_t c = 0; c < kLetterCount; ++c) {
        const uint8_t a = alpha_[letterIndexFromCell(c)];
        if (a) dst.setCell(c, lerp(dst.cell(c), color_.cell(c), a));
    }
    for (uint8_t d = 0; d < kDotCount; ++d) {
        const uint8_t a = alpha_[dotIndex(d)];
        if (a) dst.setDot(d, lerp(dst.dot(d), color_.dot(d), a));
    }
}

}  // namespace wordclock
