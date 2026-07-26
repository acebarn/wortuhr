#include "wordclock/Notify.h"

#include <cstring>

namespace wordclock {
namespace {

void copyBounded(char* dst, const char* src, uint8_t cap) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    uint8_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Rhythmen der Benachrichtigungen. Bewusst andere Perioden als der
// Gesundheitskanal auf den Eckpunkten -- die beiden sollen nicht im
// Gleichtakt laufen und dadurch verschmelzen.
constexpr uint32_t kPulsePeriodMs = 2200;
constexpr uint32_t kBlinkPeriodMs = 700;
constexpr uint8_t kPulseFloor = 70;

}  // namespace

const char* styleName(NotifyStyle s) {
    switch (s) {
        case NotifyStyle::Tint: return "tint";
        case NotifyStyle::Pulse: return "pulse";
        case NotifyStyle::Blink: return "blink";
        case NotifyStyle::Glyph: return "glyph";
        case NotifyStyle::Word: return "word";
        case NotifyStyle::Anim: return "anim";
    }
    return "tint";
}

bool parseStyle(const char* name, NotifyStyle& out) {
    if (!name) return false;
    struct Entry {
        const char* n;
        NotifyStyle s;
    };
    static const Entry kTable[] = {
        {"tint", NotifyStyle::Tint},   {"pulse", NotifyStyle::Pulse},
        {"blink", NotifyStyle::Blink}, {"glyph", NotifyStyle::Glyph},
        {"word", NotifyStyle::Word},   {"anim", NotifyStyle::Anim},
    };
    for (const Entry& e : kTable) {
        if (std::strcmp(name, e.n) == 0) {
            out = e.s;
            return true;
        }
    }
    return false;
}

int8_t NotifyStack::indexOf(const char* id) const {
    if (!id) return -1;
    for (uint8_t i = 0; i < count_; ++i)
        if (std::strcmp(items_[i].id, id) == 0) return int8_t(i);
    return -1;
}

int8_t NotifyStack::weakestIndex() const {
    if (count_ == 0) return -1;
    int8_t weakest = 0;
    for (uint8_t i = 1; i < count_; ++i)
        if (items_[i].prio < items_[weakest].prio) weakest = int8_t(i);
    return weakest;
}

bool NotifyStack::push(const NotifyRequest& req, uint32_t nowMs) {
    if (!req.id || !req.id[0]) return false;

    int8_t slot = indexOf(req.id);

    if (slot < 0) {
        if (count_ < kMaxNotifications) {
            slot = int8_t(count_++);
        } else {
            // Voll: nur verdraengen, wenn der Neue wirklich wichtiger ist.
            const int8_t weakest = weakestIndex();
            if (weakest < 0 || items_[weakest].prio >= req.prio) return false;
            slot = weakest;
        }
    }

    Notification& n = items_[uint8_t(slot)];
    n = Notification{};
    copyBounded(n.id, req.id, kNotifyIdLen);
    n.prio = req.prio;
    n.style = req.style;
    n.color = req.color;
    n.ttlSeconds = req.ttlSeconds;
    n.startedMs = nowMs;  // erneutes Senden frischt die Restlaufzeit auf
    n.word = req.word;
    n.sequence = nextSequence_++;
    copyBounded(n.anim, req.anim, kNotifyAnimLen);
    if (req.glyph) std::memcpy(n.glyph, req.glyph, kGlyphBytes);

    return true;
}

bool NotifyStack::clear(const char* id) {
    const int8_t i = indexOf(id);
    if (i < 0) return false;
    for (uint8_t k = uint8_t(i); k + 1 < count_; ++k) items_[k] = items_[k + 1];
    --count_;
    return true;
}

void NotifyStack::clearAll() { count_ = 0; }

void NotifyStack::tick(uint32_t nowMs, bool mqttConnected) {
    if (mqttConnected || !mqttSeen_) {
        lastMqttSeenMs_ = nowMs;
        if (mqttConnected) mqttSeen_ = true;
    }

    // Selbstheilung: ist der Broker lange weg, raeumt sich der Stapel selbst
    // ab. Auch Dauerzustaende -- sonst ueberlebt ein verlorenes clear-Telegramm
    // beliebig lange.
    if (mqttSeen_ && (nowMs - lastMqttSeenMs_) >= kOfflineExpiryMs) {
        clearAll();
        return;
    }

    uint8_t w = 0;
    for (uint8_t r = 0; r < count_; ++r) {
        const Notification& n = items_[r];
        const bool expired = n.ttlSeconds > 0 && (nowMs - n.startedMs) >= n.ttlSeconds * 1000UL;
        if (!expired) {
            if (w != r) items_[w] = n;
            ++w;
        }
    }
    count_ = w;
}

const Notification* NotifyStack::top() const {
    if (count_ == 0) return nullptr;
    const Notification* best = &items_[0];
    for (uint8_t i = 1; i < count_; ++i) {
        // Bei gleicher Prioritaet gewinnt der zuletzt eingetroffene.
        if (items_[i].prio > best->prio ||
            (items_[i].prio == best->prio && items_[i].sequence > best->sequence)) {
            best = &items_[i];
        }
    }
    return best;
}

const Notification* NotifyStack::find(const char* id) const {
    const int8_t i = indexOf(id);
    return i < 0 ? nullptr : &items_[uint8_t(i)];
}

const char* NotifyStack::activeAnimation() const {
    const Notification* n = top();
    if (!n || n->style != NotifyStyle::Anim || !n->anim[0]) return nullptr;
    return n->anim;
}

void NotifyStack::apply(Modifiers& mod, Overlay& ov, uint32_t nowMs,
                        bool breathingActive) const {
    const Notification* n = top();
    if (!n) return;

    // Atmet das Wortfeld ohnehin im Sekundentakt, weicht Pulse auf Blink aus.
    NotifyStyle style = n->style;
    if (style == NotifyStyle::Pulse && breathingActive) style = NotifyStyle::Blink;

    switch (style) {
        case NotifyStyle::Tint:
            mod.recolor = true;
            mod.tintColor = n->color;
            break;

        case NotifyStyle::Pulse: {
            mod.recolor = true;
            mod.tintColor = n->color;
            const uint8_t eased = breathCurve(phaseOf(nowMs, kPulsePeriodMs));
            mod.modulation = uint8_t(kPulseFloor + (uint16_t(255 - kPulseFloor) * eased) / 255);
            break;
        }

        case NotifyStyle::Blink: {
            mod.recolor = true;
            mod.tintColor = n->color;
            mod.modulation = ((nowMs / (kBlinkPeriodMs / 2)) % 2) ? 0 : 255;
            break;
        }

        case NotifyStyle::Glyph:
            // Bitmap in Leserichtung. Nur gesetzte Bits decken ab, der Rest
            // der Uhrzeit bleibt sichtbar.
            for (uint16_t c = 0; c < kLetterCount; ++c) {
                if ((n->glyph[c / 8] >> (c % 8)) & 1) ov.setCell(c, n->color, 255);
            }
            break;

        case NotifyStyle::Word:
            if (n->word < kWordCount) {
                const WordSpan sp = kWordSpan[n->word];
                for (uint8_t i = 0; i < sp.len; ++i)
                    ov.setCell(uint16_t(sp.cell + i), n->color, 255);
            }
            break;

        case NotifyStyle::Anim:
            // Ersetzt die Basis-Ebene; das erledigt der Aufrufer ueber
            // activeAnimation(). Hier ist nichts zu tun.
            break;
    }

    // Die Eckpunkte werden bewusst nie beschrieben: sie gehoeren dem
    // Gesundheitskanal (DESIGN 4, 8.4).
}

}  // namespace wordclock
