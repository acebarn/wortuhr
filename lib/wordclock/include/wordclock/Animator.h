#pragma once

#include <cstdint>

#include "wordclock/Frame.h"

namespace wordclock {

// Vollflaechige Animationen (DESIGN 7.2, 7.3).
//
// Sie verdecken zwangslaeufig die Uhrzeit -- 110 Pixel sind gleichzeitig die
// Buchstaben. Deshalb sind sie an Anlaesse gebunden: Stundenschlag,
// HA-Auslloesung, Dauermodus. Kein waehlbarer Modus, der dauerhaft mit der
// Uhrzeit um dieselbe Flaeche konkurriert -- daran ist die Spirale im
// Altprojekt gescheitert.
//
// Die Primitive sind parametriert, nicht handgetunt: ein neues Aussehen kostet
// YAML in HomeAssistant, keinen Flashvorgang.

enum class AnimKind : uint8_t {
    Wipe,      // gerichtete Fuellung
    Fall,      // herabfallende Spuren
    Ripple,    // Welle von einem Punkt
    Wave,      // Farbwelle unter einem Winkel
    Noise,     // langsam wanderndes Farbrauschen
    Sparkle,   // Funkeln einzelner Buchstaben
    Fire,      // Waermemodell
    Rainbow,   // Spektrum, das aus der Mitte quillt
    Spiral,    // Arme, die sich um die Mitte drehen
    Comet,     // Kopf auf einer Bahn, mit Schweif
    Confetti,  // Funkeln, jede Zelle in eigener Spektralfarbe
    Count
};

inline constexpr uint8_t kAnimKindCount = uint8_t(AnimKind::Count);

const char* animName(AnimKind k);

struct AnimParams {
    // Rainbow ist das einzige Primitiv ohne Stuetzfarben: seine Palette ist das
    // Spektrum selbst. Dort bleiben from/to wirkungslos.
    Rgb from{255, 140, 0};
    Rgb to{255, 0, 80};

    uint8_t speed = 128;      // 0 = Stillstand, 255 = sehr schnell
    uint8_t density = 128;    // Fall, Sparkle, Fire, Confetti: wie viel gleichzeitig
                              // Rainbow: wie stark die Ringe wabern
                              // Spiral: Zahl der Arme
    uint8_t scale = 128;      // Wave, Noise, Ripple, Rainbow, Spiral: raeumliche Feinheit
                              // Comet: Radius der Bahn
    uint8_t decay = 128;      // Fall, Sparkle, Comet, Confetti: Laenge der Spur
    uint8_t direction = 0;    // Wipe/Fall: 0 unten, 1 oben, 2 links, 3 rechts
                              // Wave: Winkel in Achtelschritten
    uint8_t originX = 5;      // Ripple, Rainbow, Spiral, Comet
    uint8_t originY = 5;
};

// Benannte Parameterbuendel. Kein eigener Code -- ein Preset ist Daten.
struct AnimPreset {
    const char* name;
    AnimKind kind;
    AnimParams params;
};

extern const AnimPreset kAnimPresets[];
extern const uint8_t kAnimPresetCount;

// Obergrenze zur Uebersetzungszeit. kAnimPresetCount ist eine Laufzeitgroesse
// und taugt nicht als Feldgrenze; wer die Presets durchmischen will, braucht
// aber ein Feld. Ein Test haelt fest, dass die Zahl reicht.
inline constexpr uint8_t kAnimPresetMax = 32;

// Loest einen Namen auf: entweder ein Primitiv ("wave") oder ein Preset
// ("matrix"). Presets duerfen ihre Parameter mitbringen.
bool resolveAnim(const char* name, AnimKind& kind, AnimParams& params);

class Animator {
public:
    void start(AnimKind kind, const AnimParams& params, uint32_t nowMs);
    bool startByName(const char* name, uint32_t nowMs);

    // Ueberschreibt das ganze Buchstabenfeld. Die Eckpunkte bleiben unberuehrt
    // -- sie gehoeren dem Gesundheitskanal (DESIGN 4).
    void render(Frame& out, uint32_t nowMs) const;

    bool running() const { return running_; }
    void stop() { running_ = false; }

    AnimKind kind() const { return kind_; }
    const AnimParams& params() const { return params_; }

private:
    void renderWipe(Frame&, uint32_t t) const;
    void renderFall(Frame&, uint32_t t) const;
    void renderRipple(Frame&, uint32_t t) const;
    void renderWave(Frame&, uint32_t t) const;
    void renderNoise(Frame&, uint32_t t) const;
    void renderSparkle(Frame&, uint32_t t) const;
    void renderFire(Frame&, uint32_t t) const;
    void renderRainbow(Frame&, uint32_t t) const;
    void renderSpiral(Frame&, uint32_t t) const;
    void renderComet(Frame&, uint32_t t) const;
    void renderConfetti(Frame&, uint32_t t) const;

    Rgb shade(uint8_t v) const;

    AnimKind kind_ = AnimKind::Wave;
    AnimParams params_;
    uint32_t startMs_ = 0;
    bool running_ = false;
};

// --- gemeinsame Rechenhilfen ------------------------------------------------

// Ganzzahliger Sinus, 0..255 um 128 schwingend. Kein float: der ESP8266 hat
// keine Fliesskommaeinheit, und bei 110 Pixeln mal 20 Bildern je Sekunde
// summiert sich das.
uint8_t sin8(uint8_t angle);

// Deterministisches Rauschen. Bewusst kein rand(): der Simulator soll dasselbe
// zeigen wie das Geraet, und ein Test soll wiederholbar sein.
uint8_t hash8(uint16_t x);

// Voll gesaettigte Spektralfarbe, 0 = rot, 85 = gruen, 170 = blau, 255 wieder
// fast rot. Der Uebergang ueber den Nullpunkt ist stetig -- eine Naht waere auf
// der Frontplatte als stehender Strich sichtbar.
Rgb hue8(uint8_t h);

// Winkel eines Vektors, 0..255 im Kreis, gegen den Uhrzeigersinn ab "rechts".
//
// Genaehert: innerhalb eines Achtels wird linear interpoliert, der Fehler
// bleibt unter drei Grad. Ein echtes atan2 kostete auf dem ESP8266 eine
// Fliesskomma-Emulation je Pixel -- bei 110 Pixeln und 20 Bildern je Sekunde
// ist das der Unterschied zwischen fluessig und ruckelig.
uint8_t angle8(int16_t x, int16_t y);

}  // namespace wordclock
