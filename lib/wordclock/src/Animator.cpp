#include "wordclock/Animator.h"

#include <cstring>

namespace wordclock {
namespace {

// Viertelwelle eines Sinus, 0..255. Der Rest wird gespiegelt -- spart drei
// Viertel der Tabelle.
constexpr uint8_t kSinQuarter[65] = {
    0,   6,   13,  19,  25,  31,  38,  44,  50,  56,  62,  68,  74,  80,  86,  92,  98,
    104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177, 181, 185,
    190, 194, 198, 202, 205, 209, 212, 215, 219, 221, 224, 227, 229, 232, 234, 236, 238,
    240, 242, 243, 245, 246, 247, 248, 249, 250, 251, 251, 252, 252, 252};

// Fortschritt in Animationseinheiten.
//
// `divisor` legt fest, wie viele Millisekunden eine Einheit kostet; er ist je
// Primitiv aus dessen Wertebereich hergeleitet, damit ein Durchlauf bei
// mittlerem Tempo etwa eine bis drei Sekunden dauert. Zu kleine Werte lassen
// die Animation nicht "schnell" aussehen, sondern zerfallen sie in Flackern:
// wandert der Kopf einer Spur pro Bild weiter als der ganze Wertebereich,
// bleibt vom Fallen nur noch Rauschen uebrig.
//
// speed 0 heisst wirklich Stillstand -- kein Sockel.
inline uint32_t phase(uint32_t t, uint8_t speed, uint32_t divisor) {
    if (speed == 0) return 0;
    return (t * uint32_t(speed)) / divisor;
}

inline uint8_t clamp8(int32_t v) { return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); }

}  // namespace

uint8_t sin8(uint8_t angle) {
    const uint8_t quadrant = angle >> 6;
    const uint8_t offset = angle & 0x3F;
    uint8_t v;
    switch (quadrant) {
        case 0: v = kSinQuarter[offset]; break;
        case 1: v = kSinQuarter[64 - offset]; break;
        case 2: v = uint8_t(255 - kSinQuarter[offset]); break;
        default: v = uint8_t(255 - kSinQuarter[64 - offset]); break;
    }
    // Auf 0..255 um die Mitte legen.
    return quadrant < 2 ? uint8_t(128 + v / 2) : uint8_t(128 - (255 - v) / 2);
}

uint8_t hash8(uint16_t x) {
    uint32_t h = uint32_t(x) * 2654435761u;
    h ^= h >> 13;
    h *= 1274126177u;
    return uint8_t(h >> 24);
}

// Sechs lineare Rampen. Die Sektorbreite wird gerechnet und nicht auf 43
// gerundet, sonst klaffte am Wechsel von 255 nach 0 eine Luecke von rund einem
// Drittel Blau -- auf der Frontplatte ein stehender Strich, genau dort, wo der
// Regenbogen nahtlos wirken soll.
//
// Bewusst ohne Helligkeitsausgleich fuer Gelb und Cyan: die Milchglasscheibe
// mischt ohnehin, und ein Ausgleich kostet jede Zelle eine weitere Rechnung.
Rgb hue8(uint8_t h) {
    const uint16_t s = uint16_t(h) * 6;
    const uint8_t sector = uint8_t(s >> 8);
    const uint8_t r = uint8_t(s & 0xFF);  // Position innerhalb des Sechstels
    switch (sector) {
        case 0: return {255, r, 0};
        case 1: return {uint8_t(255 - r), 255, 0};
        case 2: return {0, 255, r};
        case 3: return {0, uint8_t(255 - r), 255};
        case 4: return {r, 0, 255};
        default: return {255, 0, uint8_t(255 - r)};
    }
}

const char* animName(AnimKind k) {
    switch (k) {
        case AnimKind::Wipe: return "wipe";
        case AnimKind::Fall: return "fall";
        case AnimKind::Ripple: return "ripple";
        case AnimKind::Wave: return "wave";
        case AnimKind::Noise: return "noise";
        case AnimKind::Sparkle: return "sparkle";
        case AnimKind::Fire: return "fire";
        case AnimKind::Rainbow: return "rainbow";
        case AnimKind::Count: break;
    }
    return "wave";
}

// --- Presets ---------------------------------------------------------------
//
// Reine Daten. "Feuer" bekommt trotzdem ein eigenes Primitiv: ein ueberzeugendes
// Feuer braucht ein Waermemodell -- jede Zelle kuehlt ab, Hitze steigt auf,
// unten wird nachgeschuert. Mit Rauschen und einer Palette sieht man den
// Unterschied sofort.

namespace {
constexpr AnimParams p(Rgb from, Rgb to, uint8_t speed, uint8_t density, uint8_t scale,
                       uint8_t decay, uint8_t dir) {
    AnimParams a;
    a.from = from;
    a.to = to;
    a.speed = speed;
    a.density = density;
    a.scale = scale;
    a.decay = decay;
    a.direction = dir;
    return a;
}
}  // namespace

const AnimPreset kAnimPresets[] = {
    {"matrix", AnimKind::Fall, p({0, 255, 70}, {0, 60, 20}, 110, 70, 128, 190, 0)},
    // Nordlicht: gruen dominant, Violett nur an den Raendern. Breite, langsame
    // Baender (kleines scale = wenige, weite Wellen) -- so unterscheidet es
    // sich auch in der Form von plasma, nicht nur in der Farbe.
    {"nordlicht", AnimKind::Wave, p({0, 255, 110}, {60, 0, 180}, 22, 128, 30, 128, 1)},
    // Silvester: `to` ist die Farbe im Augenblick des Aufblitzens, `from` die
    // des Verglimmens -- beim Feuerwerk also weiss nach farbig, nicht umgekehrt.
    // Dichte deutlich hoeher: bei 90 waren nur rund 13 der 110 Zellen beteiligt,
    // das wirkte schuechtern statt festlich.
    {"silvester", AnimKind::Sparkle, p({255, 70, 170}, {255, 245, 210}, 190, 175, 128, 205, 0)},
    {"sonnenaufgang", AnimKind::Wipe, p({255, 60, 0}, {255, 200, 90}, 30, 128, 128, 128, 1)},
    {"feuer", AnimKind::Fire, p({255, 40, 0}, {255, 230, 140}, 150, 150, 128, 128, 0)},
    {"welle", AnimKind::Wave, p({255, 150, 40}, {200, 0, 90}, 70, 128, 110, 128, 0)},
    {"tropfen", AnimKind::Ripple, p({120, 200, 255}, {0, 30, 90}, 90, 128, 120, 128, 0)},
    // Plasma: kraeftiger Magenta-Cyan-Kontrast und feine, schnelle Turbulenz --
    // bewusst weit weg vom Nordlicht, das breit, langsam und gruen ist.
    {"plasma", AnimKind::Noise, p({255, 0, 90}, {0, 200, 255}, 70, 128, 150, 128, 0)},
    // Regenbogen: langsames Farbwabern aus der Mitte heraus. from/to sind hier
    // wirkungslos -- die Palette ist das Spektrum. Sie stehen trotzdem da, weil
    // ein Preset ein vollstaendiger Parametersatz ist; wer ihn kopiert und die
    // Art aendert, faengt nicht bei Schwarz an.
    // Kleines scale = weite Ringe: auf 11x10 passen sonst zu viele Farben
    // nebeneinander und das Spektrum zerfaellt in Konfetti.
    {"regenbogen", AnimKind::Rainbow, p({255, 0, 0}, {0, 90, 255}, 26, 170, 60, 128, 0)},
};
const uint8_t kAnimPresetCount = sizeof(kAnimPresets) / sizeof(kAnimPresets[0]);

bool resolveAnim(const char* name, AnimKind& kind, AnimParams& params) {
    if (!name || !*name) return false;

    for (uint8_t i = 0; i < kAnimPresetCount; ++i) {
        if (!std::strcmp(name, kAnimPresets[i].name)) {
            kind = kAnimPresets[i].kind;
            params = kAnimPresets[i].params;
            return true;
        }
    }
    for (uint8_t i = 0; i < kAnimKindCount; ++i) {
        if (!std::strcmp(name, animName(AnimKind(i)))) {
            kind = AnimKind(i);
            params = AnimParams{};
            return true;
        }
    }
    return false;
}

// --- Animator ---------------------------------------------------------------

void Animator::start(AnimKind kind, const AnimParams& params, uint32_t nowMs) {
    kind_ = kind;
    params_ = params;
    startMs_ = nowMs;
    running_ = true;
}

bool Animator::startByName(const char* name, uint32_t nowMs) {
    AnimKind k;
    AnimParams p;
    if (!resolveAnim(name, k, p)) return false;
    start(k, p, nowMs);
    return true;
}

Rgb Animator::shade(uint8_t v) const { return scale(lerp(params_.from, params_.to, v), v); }

void Animator::render(Frame& out, uint32_t nowMs) const {
    if (!running_) return;
    const uint32_t t = nowMs - startMs_;

    out.fillLetters(kBlack);

    switch (kind_) {
        case AnimKind::Wipe: renderWipe(out, t); break;
        case AnimKind::Fall: renderFall(out, t); break;
        case AnimKind::Ripple: renderRipple(out, t); break;
        case AnimKind::Wave: renderWave(out, t); break;
        case AnimKind::Noise: renderNoise(out, t); break;
        case AnimKind::Sparkle: renderSparkle(out, t); break;
        case AnimKind::Fire: renderFire(out, t); break;
        case AnimKind::Rainbow: renderRainbow(out, t); break;
        case AnimKind::Count: break;
    }
}

// Eine wandernde Front. Dahinter volle Helligkeit, davor dunkel, am Rand ein
// weicher Uebergang -- eine harte Kante sieht auf Milchglas billig aus.
void Animator::renderWipe(Frame& out, uint32_t t) const {
    const bool vertical = params_.direction < 2;
    const uint8_t extent = vertical ? kHeight : kWidth;
    const uint32_t span = uint32_t(extent) * 64;
    const uint32_t pos = phase(t, params_.speed, 260) % (span + 64 * 6);

    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            uint8_t along = vertical ? y : x;
            if (params_.direction == 1 || params_.direction == 3)
                along = uint8_t(extent - 1 - along);

            const int32_t d = int32_t(pos) - int32_t(uint32_t(along) * 64);
            uint8_t v;
            if (d < 0) v = 0;
            else if (d > 64 * 4) v = 0;                       // Front bereits vorbei
            else v = clamp8(255 - (d * 255) / (64 * 4));      // weicher Nachlauf
            if (v) out.setXY(x, y, shade(v));
        }
    }
}

// Herabfallende Spuren. Jede Spalte hat eigenes Tempo und eigenen Versatz,
// sonst faellt alles im Gleichschritt und wirkt wie ein Vorhang.
void Animator::renderFall(Frame& out, uint32_t t) const {
    const uint8_t trail = uint8_t(2 + params_.decay / 48);  // 2..7 Zellen

    for (uint8_t x = 0; x < kWidth; ++x) {
        const uint8_t seed = hash8(x);
        if (seed > params_.density + 60) continue;  // diese Spalte ruht

        const uint8_t colSpeed = uint8_t(params_.speed / 2 + (hash8(uint16_t(x + 40)) >> 3));
        const uint32_t head = phase(t, colSpeed, 9000) + hash8(uint16_t(x + 90));
        const int32_t hy = int32_t(head % (kHeight + trail + 4));

        for (uint8_t k = 0; k <= trail; ++k) {
            const int32_t y = hy - k;
            if (y < 0 || y >= kHeight) continue;
            const uint8_t v = clamp8(255 - int32_t(k) * (255 / (trail + 1)));
            out.setXY(x, uint8_t(y), shade(v));
        }
    }
}

// Ringe, die vom Ursprung nach aussen laufen.
void Animator::renderRipple(Frame& out, uint32_t t) const {
    const uint32_t r = phase(t, params_.speed, 530);
    const uint8_t k = uint8_t(4 + params_.scale / 24);

    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const int32_t dx = int32_t(x) - params_.originX;
            const int32_t dy = int32_t(y) - params_.originY;
            // Ganzzahlige Naeherung des Abstands: max + halbes min. Genau genug
            // fuer 11x10 und ohne Wurzel.
            const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            const int32_t dist = (ax > ay ? ax : ay) + (ax > ay ? ay : ax) / 2;

            const uint8_t v = sin8(uint8_t(uint32_t(dist) * k - r));
            out.setXY(x, y, shade(v));
        }
    }
}

// Farbwelle unter einem Winkel. Der Winkel kommt in Achtelschritten.
void Animator::renderWave(Frame& out, uint32_t t) const {
    const uint32_t p = phase(t, params_.speed, 800);
    const uint8_t k = uint8_t(6 + params_.scale / 16);
    const uint8_t angle = uint8_t(params_.direction * 32);
    const int16_t cx = int16_t(sin8(uint8_t(angle + 64))) - 128;  // cos
    const int16_t cy = int16_t(sin8(angle)) - 128;

    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const int32_t proj = (int32_t(x) * cx + int32_t(y) * cy) / 64;
            const uint8_t v = sin8(uint8_t(proj * k - int32_t(p)));
            out.setXY(x, y, shade(v));
        }
    }
}

// Ueberlagerte Wellen ergeben ein wanderndes Muster, das nie exakt wiederkehrt.
void Animator::renderNoise(Frame& out, uint32_t t) const {
    const uint32_t p = phase(t, params_.speed, 1600);
    const uint8_t k = uint8_t(8 + params_.scale / 12);

    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const uint16_t a = sin8(uint8_t(x * k + p));
            const uint16_t b = sin8(uint8_t(y * k - p * 3 / 4));
            const uint16_t c = sin8(uint8_t((x + y) * (k / 2) + p / 2));
            out.setXY(x, y, shade(uint8_t((a + b + c) / 3)));
        }
    }
}

// Einzelne Buchstaben blitzen auf und klingen ab. Die Phase je Zelle haengt an
// ihrem Hash, damit das Funkeln ohne gespeicherten Zustand auskommt.
void Animator::renderSparkle(Frame& out, uint32_t t) const {
    const uint32_t p = phase(t, params_.speed, 1100);
    const uint8_t period = uint8_t(60 + (255 - params_.decay) / 2);

    for (uint16_t c = 0; c < kLetterCount; ++c) {
        const uint8_t seed = hash8(c);
        if (seed > params_.density) continue;

        const uint8_t local = uint8_t((p + hash8(uint16_t(c + 500))) % period);
        if (local > period / 3) continue;  // die meiste Zeit dunkel

        const uint8_t v = clamp8(255 - int32_t(local) * 255 / (period / 3));
        out.setCell(c, shade(v));
    }
}

// Waermemodell statt Rauschen: jede Zelle kuehlt mit der Hoehe ab, unten wird
// nachgeschuert. Ohne den Aufstieg sieht Feuer sofort wie flackerndes Rauschen
// aus -- deshalb bekommt es als einziges Primitiv eine eigene Rechnung.
void Animator::renderFire(Frame& out, uint32_t t) const {
    const uint32_t p = phase(t, params_.speed, 210);

    for (uint8_t x = 0; x < kWidth; ++x) {
        // Glut am Boden, je Spalte unterschiedlich stark und in Bewegung.
        const uint8_t ember =
            uint8_t(160 + sin8(uint8_t(x * 40 + p)) / 3 +
                    (hash8(uint16_t(x * 7 + p / 6)) >> 3));

        for (uint8_t y = 0; y < kHeight; ++y) {
            const uint8_t up = uint8_t(kHeight - 1 - y);  // 0 unten
            // Abkuehlung mit der Hoehe, moduliert durch wanderndes Rauschen.
            const int32_t cool =
                int32_t(up) * (200 - params_.density / 2) / kHeight +
                (hash8(uint16_t(x * 31 + up * 17 + p / 4)) >> 3);
            const uint8_t heat = clamp8(int32_t(ember) - cool);
            if (!heat) continue;

            // Heisser Kern laeuft ins Helle aus.
            const Rgb base = lerp(params_.from, params_.to, heat);
            out.setXY(x, y, scale(base, heat));
        }
    }
}

// Spektrum, das aus der Mitte quillt. Der Farbton haengt am Abstand zum
// Ursprung, die Zeit zieht ihn nach aussen -- wie ripple, aber die Ringe sind
// nicht hell und dunkel, sondern rot und gruen und blau.
//
// Zwei Dinge unterscheiden es von "ripple mit Regenbogenpalette":
//
// Erstens verbiegen zwei ungleich schnelle Wellen den Abstand, bevor daraus ein
// Farbton wird. Exakte Kreise sehen auf 11x10 nach Zielscheibe aus; die
// Verbiegung laesst die Ringe wabern statt bloss zu pulsieren, und weil beide
// Wellen unterschiedlich lange brauchen, wiederholt sich das Bild praktisch nie.
//
// Zweitens bleibt die Helligkeit fast voll. Ein Regenbogen, der zwischendurch
// nach Schwarz laeuft, verliert genau die Farben, wegen derer man ihn gewaehlt
// hat -- die leichte Schwankung gibt ihm nur Tiefe.
void Animator::renderRainbow(Frame& out, uint32_t t) const {
    // Deutlich traeger als wave: das Wabern soll man beim Hinsehen bemerken,
    // nicht beim Vorbeigehen.
    const uint32_t p = phase(t, params_.speed, 3400);
    const uint8_t k = uint8_t(8 + params_.scale / 6);  // Farbton je Abstandsschritt
    // Auslenkung in Farbtoneinheiten -- an k gekoppelt, damit sie in Zellen
    // gemessen gleich bleibt, wenn man die Ringe enger stellt. Bei mittlerer
    // Dichte verbiegt sich ein Ring um rund eine Zelle; ohne die Kopplung
    // verschluckt eine hohe Dichte bei weiten Ringen das Kreisrunde ganz.
    const int32_t swell = (int32_t(params_.density) * k) / 192;

    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const int32_t dx = int32_t(x) - params_.originX;
            const int32_t dy = int32_t(y) - params_.originY;
            // Wie bei ripple: max + halbes min statt Wurzel.
            const int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            const int32_t dist = (ax > ay ? ax : ay) + (ax > ay ? ay : ax) / 2;

            const int32_t wobble =
                (int32_t(sin8(uint8_t(x * 20 + y * 12 + p / 3))) - 128) * swell / 128 +
                (int32_t(sin8(uint8_t(y * 17 - x * 9 - p / 5))) - 128) * swell / 192;

            const uint8_t hue = uint8_t(dist * k + wobble - int32_t(p));
            const uint8_t bright = uint8_t(200 + sin8(uint8_t(dist * 12 - p / 2)) / 5);
            out.setXY(x, y, scale(hue8(hue), bright));
        }
    }
}

}  // namespace wordclock
