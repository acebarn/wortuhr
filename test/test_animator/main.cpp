// Host-Tests fuer die Animations-Primitive.
//
//   pio test -e native

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "wordclock/Animator.h"

using namespace wordclock;

void setUp(void) {}
void tearDown(void) {}

static char g_msg[256];
static const char* msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(g_msg, sizeof(g_msg), fmt, a);
    va_end(a);
    return g_msg;
}

static uint16_t litCells(const Frame& f) {
    uint16_t n = 0;
    for (uint16_t c = 0; c < kLetterCount; ++c)
        if (f.cell(c) != kBlack) ++n;
    return n;
}

// --- Rechenhilfen ----------------------------------------------------------

static void test_sin8_shape() {
    // Um 128 schwingend, Extremwerte bei einem und drei Vierteln.
    TEST_ASSERT_UINT8_WITHIN(3, 128, sin8(0));
    TEST_ASSERT_TRUE_MESSAGE(sin8(64) > 240, msg("Hochpunkt ist %u", sin8(64)));
    TEST_ASSERT_UINT8_WITHIN(3, 128, sin8(128));
    TEST_ASSERT_TRUE_MESSAGE(sin8(192) < 15, msg("Tiefpunkt ist %u", sin8(192)));

    // Stetig: keine Spruenge groesser als ein Achtel des Wertebereichs.
    for (int a = 0; a < 256; ++a) {
        const int d = int(sin8(uint8_t((a + 1) & 0xFF))) - int(sin8(uint8_t(a)));
        TEST_ASSERT_TRUE_MESSAGE(d < 32 && d > -32, msg("Sprung bei %d: %d", a, d));
    }
}

// Kein rand(): Simulator und Geraet sollen dasselbe zeigen, und ein Test soll
// wiederholbar sein.
static void test_hash_is_deterministic_and_spread() {
    TEST_ASSERT_EQUAL_UINT8(hash8(42), hash8(42));

    uint16_t buckets[4] = {0, 0, 0, 0};
    for (uint16_t i = 0; i < 1000; ++i) ++buckets[hash8(i) / 64];
    for (uint8_t b = 0; b < 4; ++b)
        TEST_ASSERT_TRUE_MESSAGE(buckets[b] > 150, msg("Viertel %u nur %u mal", b, buckets[b]));
}

// --- Aufloesung von Namen ---------------------------------------------------

static void test_primitive_names_round_trip() {
    for (uint8_t i = 0; i < kAnimKindCount; ++i) {
        AnimKind k;
        AnimParams p;
        TEST_ASSERT_TRUE_MESSAGE(resolveAnim(animName(AnimKind(i)), k, p), animName(AnimKind(i)));
        TEST_ASSERT_EQUAL_UINT8(i, uint8_t(k));
    }
    AnimKind k;
    AnimParams p;
    TEST_ASSERT_FALSE(resolveAnim("gibtsnicht", k, p));
    TEST_ASSERT_FALSE(resolveAnim(nullptr, k, p));
    TEST_ASSERT_FALSE(resolveAnim("", k, p));
}

// Ein Preset ist ein benanntes Parameterbuendel, kein eigener Code.
static void test_presets_resolve_and_are_distinct() {
    std::set<std::string> names;
    for (uint8_t i = 0; i < kAnimPresetCount; ++i) {
        const AnimPreset& pr = kAnimPresets[i];
        TEST_ASSERT_TRUE_MESSAGE(names.insert(pr.name).second, msg("%s doppelt", pr.name));

        AnimKind k;
        AnimParams p;
        TEST_ASSERT_TRUE_MESSAGE(resolveAnim(pr.name, k, p), pr.name);
        TEST_ASSERT_TRUE_MESSAGE(k == pr.kind, pr.name);
        TEST_ASSERT_TRUE_MESSAGE(p.speed == pr.params.speed, pr.name);
    }
    TEST_ASSERT_TRUE_MESSAGE(kAnimPresetCount >= 5, "es sollten mehrere Presets existieren");

    // Zwei Presets mit identischen Werten waeren ein Kopierfehler: in der
    // Auswahl staenden zwei Namen, die dasselbe zeigen.
    for (uint8_t i = 0; i < kAnimPresetCount; ++i) {
        for (uint8_t j = uint8_t(i + 1); j < kAnimPresetCount; ++j) {
            const AnimParams& a = kAnimPresets[i].params;
            const AnimParams& b = kAnimPresets[j].params;
            const bool same = kAnimPresets[i].kind == kAnimPresets[j].kind && a.from == b.from &&
                              a.to == b.to && a.speed == b.speed && a.density == b.density &&
                              a.scale == b.scale && a.decay == b.decay &&
                              a.direction == b.direction;
            TEST_ASSERT_FALSE_MESSAGE(same, msg("%s und %s sind identisch", kAnimPresets[i].name,
                                                kAnimPresets[j].name));
        }
    }
}

// --- Verhalten aller Primitive ---------------------------------------------

// Jedes Primitiv muss ueber die Zeit etwas zeigen, sich bewegen und dabei
// niemals die Eckpunkte anfassen -- die gehoeren dem Gesundheitskanal.
static void test_every_primitive_animates_and_spares_the_dots() {
    for (uint8_t i = 0; i < kAnimKindCount; ++i) {
        const AnimKind kind = AnimKind(i);
        Animator a;
        a.start(kind, AnimParams{}, 0);

        Frame f;
        f.clear();
        for (uint8_t d = 0; d < kDotCount; ++d) f.setDot(d, {9, 8, 7});

        uint16_t maxLit = 0;
        std::set<std::string> seen;

        for (uint32_t t = 0; t < 6000; t += 60) {
            a.render(f, t);

            for (uint8_t d = 0; d < kDotCount; ++d)
                TEST_ASSERT_TRUE_MESSAGE(f.dot(d) == Rgb({9, 8, 7}),
                                         msg("%s hat Punkt %u angefasst", animName(kind), d));

            const uint16_t lit = litCells(f);
            if (lit > maxLit) maxLit = lit;

            std::string sig;
            for (uint16_t c = 0; c < kLetterCount; c += 7)
                sig += char('0' + (f.cell(c).r + f.cell(c).g + f.cell(c).b) / 96);
            seen.insert(sig);
        }

        TEST_ASSERT_TRUE_MESSAGE(maxLit > 8, msg("%s zeigt fast nichts (%u Zellen)",
                                                 animName(kind), maxLit));
        TEST_ASSERT_TRUE_MESSAGE(seen.size() > 4, msg("%s bewegt sich kaum (%zu Bilder)",
                                                      animName(kind), seen.size()));
    }
}

static void test_nothing_is_drawn_before_start() {
    Animator a;
    Frame f;
    f.clear();
    f.fillLetters({40, 40, 40});
    a.render(f, 1000);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(kLetterCount, litCells(f),
                                     "ohne Start darf nichts uebermalt werden");

    a.start(AnimKind::Wave, AnimParams{}, 0);
    a.stop();
    a.render(f, 1000);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(kLetterCount, litCells(f), "nach stop() ebenso");
}

static void test_speed_zero_stands_still() {
    AnimParams p;
    p.speed = 0;
    Animator a;
    a.start(AnimKind::Wave, p, 0);

    Frame first, later;
    first.clear();
    later.clear();
    a.render(first, 0);
    a.render(later, 4000);

    for (uint16_t c = 0; c < kLetterCount; ++c)
        TEST_ASSERT_TRUE_MESSAGE(first.cell(c) == later.cell(c),
                                 msg("Zelle %u bewegt sich trotz Tempo 0", c));
}

static void test_palette_is_respected() {
    AnimParams p;
    p.from = {255, 0, 0};
    p.to = {255, 0, 0};  // beide rot -> nichts Gruenes oder Blaues darf auftauchen
    Animator a;
    a.start(AnimKind::Noise, p, 0);

    Frame f;
    f.clear();
    for (uint32_t t = 0; t < 3000; t += 90) {
        a.render(f, t);
        for (uint16_t c = 0; c < kLetterCount; ++c) {
            const Rgb v = f.cell(c);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, v.g, msg("Zelle %u hat Gruenanteil", c));
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, v.b, msg("Zelle %u hat Blauanteil", c));
        }
    }
}

// Feuer ist unten heiss und oben kuehl -- genau das unterscheidet ein
// Waermemodell von Rauschen mit Feuerpalette.
static void test_fire_is_hotter_at_the_bottom() {
    Animator a;
    a.start(AnimKind::Fire, kAnimPresets[4].params, 0);

    uint32_t bottom = 0, top = 0;
    for (uint32_t t = 0; t < 4000; t += 80) {
        Frame f;
        f.clear();
        a.render(f, t);
        for (uint8_t x = 0; x < kWidth; ++x) {
            const Rgb b = f.xy(x, kHeight - 1);
            const Rgb u = f.xy(x, 0);
            bottom += uint32_t(b.r) + b.g + b.b;
            top += uint32_t(u.r) + u.g + u.b;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bottom > top * 2,
                             msg("unten %u, oben %u -- kein Aufsteigen erkennbar",
                                 unsigned(bottom), unsigned(top)));
}

// Herabfallende Spuren muessen sich abwaerts bewegen, nicht bloss blinken.
static void test_fall_moves_downward() {
    Animator a;
    a.start(AnimKind::Fall, kAnimPresets[0].params, 0);

    int32_t rising = 0, falling = 0;
    int32_t prev[kWidth];
    for (uint8_t x = 0; x < kWidth; ++x) prev[x] = -1;

    for (uint32_t t = 0; t < 4000; t += 50) {
        Frame f;
        f.clear();
        a.render(f, t);
        for (uint8_t x = 0; x < kWidth; ++x) {
            int32_t head = -1;
            for (int8_t y = kHeight - 1; y >= 0; --y)
                if (f.xy(x, uint8_t(y)) != kBlack) { head = y; break; }
            if (head >= 0 && prev[x] >= 0) {
                if (head > prev[x]) ++falling;
                else if (head < prev[x]) ++rising;
            }
            prev[x] = head;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(falling > rising * 2,
                             msg("abwaerts %d, aufwaerts %d", falling, rising));
}

// Ein Regenbogen muss das Spektrum zeigen, nicht bloss zwei Farben -- und der
// Uebergang ueber den Nullpunkt muss stetig sein, sonst steht eine Naht im Bild.
static void test_hue8_covers_the_spectrum_without_a_seam() {
    TEST_ASSERT_TRUE_MESSAGE(hue8(0).r > 250 && hue8(0).g < 5 && hue8(0).b < 5, "0 ist nicht rot");
    TEST_ASSERT_TRUE_MESSAGE(hue8(85).g > 250 && hue8(85).r < 10, "85 ist nicht gruen");
    TEST_ASSERT_TRUE_MESSAGE(hue8(170).b > 250 && hue8(170).g < 10, "170 ist nicht blau");

    for (int h = 0; h < 256; ++h) {
        const Rgb a = hue8(uint8_t(h)), b = hue8(uint8_t((h + 1) & 0xFF));
        const int d = std::abs(int(a.r) - b.r) + std::abs(int(a.g) - b.g) + std::abs(int(a.b) - b.b);
        TEST_ASSERT_TRUE_MESSAGE(d < 24, msg("Farbsprung bei %d: %d", h, d));
    }
}

// Das Farbwabern quillt aus der Mitte: verfolgt man einen Ring einer festen
// Farbe, wandert er nach aussen. Gemessen am mittleren Abstand aller roten
// Zellen -- ein einzelner Punkt liefe dem Wabern hinterher, der Mittelwert
// nicht.
static void test_rainbow_flows_outward() {
    AnimKind kind;
    AnimParams p;
    TEST_ASSERT_TRUE(resolveAnim("regenbogen", kind, p));
    TEST_ASSERT_TRUE(kind == AnimKind::Rainbow);

    Animator a;
    a.start(kind, p, 0);

    int outward = 0, inward = 0;
    double prev = -1;

    for (uint32_t t = 0; t < 60000; t += 100) {
        Frame f;
        f.clear();
        a.render(f, t);

        double sum = 0;
        int n = 0;
        for (uint8_t y = 0; y < kHeight; ++y) {
            for (uint8_t x = 0; x < kWidth; ++x) {
                const Rgb v = f.xy(x, y);
                if (v.r < 150 || v.g > 50 || v.b > 50) continue;  // nicht rot
                const double dx = double(x) - p.originX, dy = double(y) - p.originY;
                sum += std::sqrt(dx * dx + dy * dy);
                ++n;
            }
        }
        if (!n) {  // gerade kein Rot auf der Flaeche
            prev = -1;
            continue;
        }

        const double mean = sum / n;
        // Springt der Ring, ist er hinten aus der Flaeche gelaufen und vorn neu
        // entstanden -- kein Ruecklauf, sondern der naechste Durchgang.
        if (prev >= 0 && std::fabs(mean - prev) < 0.6) {
            if (mean > prev) ++outward;
            else if (mean < prev) ++inward;
        }
        prev = mean;
    }

    TEST_ASSERT_TRUE_MESSAGE(outward > inward * 3,
                             msg("nach aussen %d, nach innen %d", outward, inward));
}

// Ein echtes Spektrum, nicht zwei Stuetzfarben: ueber die Zeit muss jede
// Grundfarbe einmal fuehren.
static void test_rainbow_shows_the_whole_spectrum() {
    AnimKind kind;
    AnimParams p;
    TEST_ASSERT_TRUE(resolveAnim("regenbogen", kind, p));
    Animator a;
    a.start(kind, p, 0);

    bool red = false, green = false, blue = false;
    for (uint32_t t = 0; t < 60000; t += 200) {
        Frame f;
        f.clear();
        a.render(f, t);
        for (uint16_t c = 0; c < kLetterCount; ++c) {
            const Rgb v = f.cell(c);
            if (v.r > 150 && v.g < 50 && v.b < 50) red = true;
            if (v.g > 150 && v.r < 50 && v.b < 50) green = true;
            if (v.b > 150 && v.r < 50 && v.g < 50) blue = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(red && green && blue,
                             msg("rot %d, gruen %d, blau %d", red, green, blue));
}

// Der Regenbogen darf nicht zwischendurch nach Schwarz laufen: dann faellt die
// Flaeche in Loecher, und genau die Farben verschwinden, wegen derer man ihn
// gewaehlt hat.
static void test_rainbow_stays_bright() {
    AnimKind k;
    AnimParams p;
    TEST_ASSERT_TRUE(resolveAnim("regenbogen", k, p));
    Animator a;
    a.start(k, p, 0);

    for (uint32_t t = 0; t < 20000; t += 310) {
        Frame f;
        f.clear();
        a.render(f, t);
        for (uint16_t c = 0; c < kLetterCount; ++c) {
            const Rgb v = f.cell(c);
            const uint8_t m = valueOf(v);
            TEST_ASSERT_TRUE_MESSAGE(m > 150, msg("Zelle %u nur %u hell bei t=%u", c, m, unsigned(t)));
        }
    }
}

static void test_wipe_direction_matters() {
    AnimParams down, up;
    down.direction = 0;
    up.direction = 1;

    Animator a, b;
    a.start(AnimKind::Wipe, down, 0);
    b.start(AnimKind::Wipe, up, 0);

    Frame fa, fb;
    fa.clear();
    fb.clear();
    a.render(fa, 600);
    b.render(fb, 600);

    bool differs = false;
    for (uint16_t c = 0; c < kLetterCount && !differs; ++c)
        if (fa.cell(c) != fb.cell(c)) differs = true;
    TEST_ASSERT_TRUE_MESSAGE(differs, "Richtung muss einen Unterschied machen");
}

// Der Simulator soll dasselbe zeigen wie das Geraet: gleiche Zeit, gleiches
// Bild, ohne verstecktem Zustand.
static void test_rendering_is_reproducible() {
    for (uint8_t i = 0; i < kAnimKindCount; ++i) {
        Animator a, b;
        a.start(AnimKind(i), AnimParams{}, 0);
        b.start(AnimKind(i), AnimParams{}, 0);

        Frame fa, fb;
        fa.clear();
        fb.clear();
        // b laeuft mit anderer Schrittweite zum selben Zeitpunkt.
        a.render(fa, 2500);
        for (uint32_t t = 0; t <= 2500; t += 125) b.render(fb, t);

        for (uint16_t c = 0; c < kLetterCount; ++c)
            TEST_ASSERT_TRUE_MESSAGE(fa.cell(c) == fb.cell(c),
                                     msg("%s: Zelle %u haengt von der Schrittweite ab",
                                         animName(AnimKind(i)), c));
    }
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sin8_shape);
    RUN_TEST(test_hash_is_deterministic_and_spread);
    RUN_TEST(test_primitive_names_round_trip);
    RUN_TEST(test_presets_resolve_and_are_distinct);
    RUN_TEST(test_every_primitive_animates_and_spares_the_dots);
    RUN_TEST(test_nothing_is_drawn_before_start);
    RUN_TEST(test_speed_zero_stands_still);
    RUN_TEST(test_palette_is_respected);
    RUN_TEST(test_fire_is_hotter_at_the_bottom);
    RUN_TEST(test_fall_moves_downward);
    RUN_TEST(test_hue8_covers_the_spectrum_without_a_seam);
    RUN_TEST(test_rainbow_flows_outward);
    RUN_TEST(test_rainbow_shows_the_whole_spectrum);
    RUN_TEST(test_rainbow_stays_bright);
    RUN_TEST(test_wipe_direction_matters);
    RUN_TEST(test_rendering_is_reproducible);
    return UNITY_END();
}
