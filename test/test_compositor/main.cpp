// Host-Tests fuer Farbmischung, Frame-Geometrie und Compositor.
//
//   pio test -e native

#include <unity.h>

#include <cstdio>
#include <set>

#include "wordclock/Compositor.h"
#include "wordclock/Frame.h"

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

// --- Farbmischung ----------------------------------------------------------

static void test_lerp_endpoints() {
    TEST_ASSERT_EQUAL_UINT8(10, lerp8(10, 200, 0));    // f=0 -> unveraendert
    TEST_ASSERT_EQUAL_UINT8(200, lerp8(10, 200, 255)); // f=255 -> sofort
    TEST_ASSERT_EQUAL_UINT8(42, lerp8(42, 42, 128));   // gleich bleibt gleich
    TEST_ASSERT_EQUAL_UINT8(128, lerp8(0, 255, 128));  // Mitte
    TEST_ASSERT_EQUAL_UINT8(127, lerp8(255, 0, 128));  // Mitte rueckwaerts
}

// Die Altfirmware blendete mit abgeschnittener Ganzzahlarithmetik ueber. Bei
// einem Restunterschied von 1 ergab der Schritt 0 und der Wert blieb dauerhaft
// eine Stufe neben dem Ziel stehen. lerp8 muss garantiert konvergieren.
static void test_lerp_always_converges() {
    for (int from = 0; from <= 255; ++from) {
        for (int to = 0; to <= 255; ++to) {
            for (uint8_t f : {uint8_t(1), uint8_t(8), uint8_t(64), uint8_t(128), uint8_t(254)}) {
                uint8_t v = uint8_t(from);
                int steps = 0;
                while (v != to && steps < 300) {
                    const uint8_t next = lerp8(v, uint8_t(to), f);
                    TEST_ASSERT_TRUE_MESSAGE(
                        next != v, msg("%d->%d f=%u bleibt bei %u stehen", from, to, f, v));
                    v = next;
                    ++steps;
                }
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(uint8_t(to), v,
                                                msg("%d->%d f=%u erreicht das Ziel nicht", from,
                                                    to, f));
            }
        }
    }
}

static void test_lerp_never_overshoots() {
    for (int from = 0; from <= 255; ++from) {
        for (int to = 0; to <= 255; ++to) {
            const uint8_t v = lerp8(uint8_t(from), uint8_t(to), 128);
            const int lo = from < to ? from : to;
            const int hi = from < to ? to : from;
            TEST_ASSERT_TRUE_MESSAGE(v >= lo && v <= hi,
                                     msg("%d->%d ergibt %u, ausserhalb", from, to, v));
        }
    }
}

static void test_modulate_white_is_neutral() {
    const Rgb c{13, 200, 77};
    TEST_ASSERT_TRUE(modulate(c, kWhite) == c);
    TEST_ASSERT_TRUE(modulate(c, kBlack) == kBlack);
}

// --- Frame -----------------------------------------------------------------

static void test_frame_maps_cells_to_strip() {
    Frame f;
    f.clear();
    f.setXY(10, 0, {1, 2, 3});  // oben rechts = Strip-Index 0
    TEST_ASSERT_TRUE(f.stripAt(0) == Rgb({1, 2, 3}));
    TEST_ASSERT_TRUE(f.xy(10, 0) == Rgb({1, 2, 3}));

    f.setXY(0, 1, {9, 9, 9});  // Zeile 1 beginnt links = Index 11
    TEST_ASSERT_TRUE(f.stripAt(11) == Rgb({9, 9, 9}));
}

static void test_frame_cell_and_xy_agree() {
    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            Frame f;
            f.clear();
            const uint16_t cellNo = uint16_t(y) * kWidth + x;
            f.setCell(cellNo, {5, 6, 7});
            TEST_ASSERT_TRUE_MESSAGE(f.xy(x, y) == Rgb({5, 6, 7}),
                                     msg("Zelle %u != (%u,%u)", cellNo, x, y));
        }
    }
}

// Das Buchstabenfeld darf die Minutenpunkte unter keinen Umstaenden treffen --
// die Verwechslung von Punktreihe und Rasterzeile war der Kernfehler der
// Altfirmware.
static void test_letters_never_touch_dots() {
    Frame f;
    f.clear();
    f.fillLetters(kWhite);
    for (uint8_t d = 0; d < kDotCount; ++d) {
        TEST_ASSERT_TRUE_MESSAGE(f.dot(d) == kBlack,
                                 msg("fillLetters hat Punkt %u eingefaerbt", d));
    }

    f.clear();
    for (uint8_t d = 0; d < kDotCount; ++d) f.setDot(d, kWhite);
    for (uint16_t c = 0; c < kLetterCount; ++c) {
        TEST_ASSERT_TRUE_MESSAGE(f.cell(c) == kBlack,
                                 msg("setDot hat Rasterzelle %u eingefaerbt", c));
    }
}

static void test_frame_covers_whole_strip() {
    // Raster plus Punkte muessen zusammen genau alle LEDs abdecken.
    std::set<uint16_t> touched;
    for (uint16_t c = 0; c < kLetterCount; ++c) touched.insert(letterIndexFromCell(c));
    for (uint8_t d = 0; d < kDotCount; ++d) touched.insert(dotIndex(d));
    TEST_ASSERT_EQUAL_UINT(kLedCount, touched.size());
}

// --- Overlay ---------------------------------------------------------------

static void test_overlay_alpha() {
    Frame base;
    base.fillLetters({0, 0, 0});

    Overlay ov;
    ov.clear();
    ov.setCell(5, kWhite, 0);    // unsichtbar
    ov.setCell(6, kWhite, 255);  // voll deckend
    ov.setCell(7, kWhite, 128);  // halb

    ov.blendInto(base);
    TEST_ASSERT_TRUE(base.cell(5) == kBlack);
    TEST_ASSERT_TRUE(base.cell(6) == kWhite);
    TEST_ASSERT_EQUAL_UINT8(128, base.cell(7).r);
}

// --- Compositor ------------------------------------------------------------

static void test_notify_cannot_reach_the_dots() {
    Frame base;
    base.clear();
    base.fillLetters({100, 100, 100});
    base.setDot(0, {200, 200, 200});

    Overlay none;
    none.clear();

    Modifiers mod;
    mod.tint = {255, 0, 0};  // wie ein notify "tint"
    mod.modulation = 64;     // wie ein notify "pulse"

    Compositor comp;
    comp.setCurrentLimit(0);  // Begrenzung hier ausblenden
    const Frame& out = comp.snap(base, none, mod);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out.cell(0).g, "tint muss das Wortfeld einfaerben");
    TEST_ASSERT_TRUE_MESSAGE(out.dot(0) == Rgb({200, 200, 200}),
                             "tint/modulation duerfen die Eckpunkte nicht anfassen");
}

static void test_brightness_affects_dots_too() {
    Frame base;
    base.clear();
    base.setDot(0, {200, 200, 200});

    Overlay none;
    none.clear();

    Modifiers mod;
    mod.brightness = 128;

    Compositor comp;
    comp.setCurrentLimit(0);
    const Frame& out = comp.snap(base, none, mod);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(100, out.dot(0).r,
                                    "globale Helligkeit muss auch die Eckpunkte dimmen");
}

static void test_smoothing_reaches_the_target() {
    Frame base;
    base.clear();
    base.fillLetters(kWhite);
    base.setDot(2, kWhite);

    Overlay none;
    none.clear();
    Modifiers mod;

    Compositor comp;
    comp.setCurrentLimit(0);
    comp.setSmoothing(128);

    for (int i = 0; i < 300; ++i) comp.step(base, none, mod);

    for (uint16_t c = 0; c < kLetterCount; ++c) {
        TEST_ASSERT_TRUE_MESSAGE(comp.current().cell(c) == comp.target().cell(c),
                                 msg("Zelle %u erreicht das Ziel nicht", c));
    }
    TEST_ASSERT_TRUE(comp.current().dot(2) == comp.target().dot(2));
}

// --- Strombegrenzung -------------------------------------------------------

static void test_current_estimate() {
    Frame f;
    f.clear();
    TEST_ASSERT_EQUAL_UINT32(0, estimateCurrentMa(f));

    f.fillAll(kWhite);
    // 114 LEDs * 3 Kanaele * 20 mA = 6840 mA
    TEST_ASSERT_EQUAL_UINT32(6840, estimateCurrentMa(f));
}

static void test_current_limit_kicks_in() {
    Frame base;
    base.clear();
    base.fillAll(kWhite);

    Overlay none;
    none.clear();
    Modifiers mod;

    Compositor comp;
    comp.setCurrentLimit(2500);
    const Frame& out = comp.snap(base, none, mod);

    TEST_ASSERT_TRUE_MESSAGE(comp.lastLimitScale() < 255, "Begrenzung haette greifen muessen");
    const uint32_t ma = estimateCurrentMa(out);
    TEST_ASSERT_TRUE_MESSAGE(ma <= 2500, msg("nach Begrenzung noch %u mA", unsigned(ma)));
}

static void test_current_limit_stays_out_when_not_needed() {
    Frame base;
    base.clear();
    base.setXY(0, 0, kWhite);

    Overlay none;
    none.clear();
    Modifiers mod;

    Compositor comp;
    comp.setCurrentLimit(2500);
    const Frame& out = comp.snap(base, none, mod);

    TEST_ASSERT_EQUAL_UINT8(255, comp.lastLimitScale());
    TEST_ASSERT_TRUE(out.xy(0, 0) == kWhite);
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_lerp_endpoints);
    RUN_TEST(test_lerp_always_converges);
    RUN_TEST(test_lerp_never_overshoots);
    RUN_TEST(test_modulate_white_is_neutral);
    RUN_TEST(test_frame_maps_cells_to_strip);
    RUN_TEST(test_frame_cell_and_xy_agree);
    RUN_TEST(test_letters_never_touch_dots);
    RUN_TEST(test_frame_covers_whole_strip);
    RUN_TEST(test_overlay_alpha);
    RUN_TEST(test_notify_cannot_reach_the_dots);
    RUN_TEST(test_brightness_affects_dots_too);
    RUN_TEST(test_smoothing_reaches_the_target);
    RUN_TEST(test_current_estimate);
    RUN_TEST(test_current_limit_kicks_in);
    RUN_TEST(test_current_limit_stays_out_when_not_needed);
    return UNITY_END();
}
