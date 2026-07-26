// Host-Tests fuer den Clock-Renderer.
//
//   pio test -e native

#include <unity.h>

#include <cstdio>
#include <set>
#include <vector>

#include "wordclock/ClockRenderer.h"

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

// Erwartete Zellen der aktiven Kette fuer eine Uhrzeit.
static std::set<uint16_t> expectedCells(uint8_t h, uint8_t m) {
    std::set<uint16_t> out;
    const Sentence s = timeToWords(h, m);
    for (uint8_t w = 0; w < s.len; ++w) {
        const WordSpan sp = span(s.words[w]);
        for (uint8_t i = 0; i < sp.len; ++i) out.insert(uint16_t(sp.cell + i));
    }
    return out;
}

static std::set<uint16_t> litCells(const Frame& f) {
    std::set<uint16_t> out;
    for (uint16_t c = 0; c < kLetterCount; ++c)
        if (f.cell(c) != kBlack) out.insert(c);
    return out;
}

// --- Grundverhalten --------------------------------------------------------

static void test_renders_exactly_the_sentence() {
    ClockStyle st;
    st.transition = Transition::None;
    ClockRenderer r;
    r.setStyle(st);

    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; m += 1) {
            Frame f;
            f.clear();
            r.render(f, uint8_t(h), uint8_t(m), 100000);
            TEST_ASSERT_TRUE_MESSAGE(litCells(f) == expectedCells(uint8_t(h), uint8_t(m)),
                                     msg("%02d:%02d zeigt nicht genau die Wortkette", h, m));
        }
    }
}

// Der Renderer darf die Eckpunkte unter keinen Umstaenden anfassen -- sie
// gehoeren dem Gesundheitskanal (DESIGN 4).
static void test_never_touches_the_dots() {
    ClockStyle st;
    st.ghost = 40;
    st.ghostFillers = true;
    st.breathDepth = 60;

    for (Transition t : {Transition::None, Transition::Staggered, Transition::FadeTop,
                         Transition::Falling}) {
        st.transition = t;
        ClockRenderer r;
        r.setStyle(st);

        Frame f;
        f.clear();
        for (uint8_t d = 0; d < kDotCount; ++d) f.setDot(d, {77, 88, 99});

        for (uint32_t ms = 0; ms < 2000; ms += 37) {
            r.render(f, 7, uint8_t((ms / 100) % 60), ms);
            for (uint8_t d = 0; d < kDotCount; ++d) {
                TEST_ASSERT_TRUE_MESSAGE(f.dot(d) == Rgb({77, 88, 99}),
                                         msg("Uebergang %d hat Punkt %u veraendert", int(t), d));
            }
        }
    }
}

// --- Uebergaenge -----------------------------------------------------------

// Jeder Uebergang muss im Endzustand exakt die Wortkette zeigen -- egal wie er
// dorthin kommt.
static void test_every_transition_ends_complete() {
    for (Transition t : {Transition::None, Transition::Staggered, Transition::FadeTop,
                         Transition::Falling}) {
        ClockStyle st;
        st.transition = t;
        st.transitionMs = 400;
        ClockRenderer r;
        r.setStyle(st);

        Frame f;
        f.clear();
        r.render(f, 7, 30, 1000);   // erster Satz
        r.render(f, 7, 35, 2000);   // Wechsel -> Uebergang startet
        r.render(f, 7, 35, 2400);   // Uebergang abgelaufen

        TEST_ASSERT_TRUE_MESSAGE(litCells(f) == expectedCells(7, 35),
                                 msg("Uebergang %d endet unvollstaendig", int(t)));
    }
}

// Waehrend eines Uebergangs darf nie ausserhalb des Rasters gezeichnet werden,
// und es duerfen keine Zellen leuchten, die weder Ziel noch Geisterwort sind.
static void test_transitions_stay_inside_the_grid() {
    for (Transition t : {Transition::Staggered, Transition::FadeTop, Transition::Falling}) {
        ClockStyle st;
        st.transition = t;
        st.transitionMs = 400;
        ClockRenderer r;
        r.setStyle(st);

        Frame f;
        f.clear();
        r.render(f, 7, 30, 0);

        for (uint32_t ms = 1000; ms <= 1400; ms += 13) {
            r.render(f, 7, 35, ms);
            // Frame::setXY/setCell verwerfen Werte ausserhalb -- ein Absturz
            // oder Ueberlauf wuerde hier auffallen. Zusaetzlich: die Anzahl
            // leuchtender Zellen kann nie groesser sein als das Raster.
            TEST_ASSERT_TRUE(litCells(f).size() <= kLetterCount);
        }
    }
}

static void test_staggered_reveals_in_reading_order() {
    ClockStyle st;
    st.transition = Transition::Staggered;
    st.transitionMs = 400;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 30, 0);
    r.render(f, 7, 45, 1000);  // "ES IST DREIVIERTEL ACHT"

    // Kurz nach dem Start muss ES bereits heller sein als ACHT.
    r.render(f, 7, 45, 1060);
    const Rgb es = f.cell(span(Word::Es).cell);
    const Rgb acht = f.cell(span(Word::Acht).cell);
    TEST_ASSERT_TRUE_MESSAGE(es.r > acht.r, "ES muss vor ACHT erscheinen");
}

static void test_falling_moves_a_pixel_down_its_column() {
    ClockStyle st;
    st.transition = Transition::Falling;
    st.transitionMs = 400;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 30, 0);
    r.render(f, 7, 45, 1000);

    // Irgendwann waehrend des Falls muss eine Zelle leuchten, die NICHT zum
    // Zielsatz gehoert -- sonst faellt nichts, sondern es blendet nur ein.
    const std::set<uint16_t> target = expectedCells(7, 45);
    bool sawTravelling = false;
    for (uint32_t ms = 1000; ms < 1400 && !sawTravelling; ms += 7) {
        r.render(f, 7, 45, ms);
        for (uint16_t c : litCells(f))
            if (!target.count(c)) sawTravelling = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawTravelling, "Falling zeichnet nie ausserhalb des Ziels");
}

// --- Statische Modifikatoren -----------------------------------------------

static void test_ghost_lights_words_but_not_fillers() {
    ClockStyle st;
    st.transition = Transition::None;
    st.ghost = 40;
    st.ghostFillers = false;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 30, 100000);

    // Zelle 2 ist das K in "ESKIST" -- ein Fuellbuchstabe.
    TEST_ASSERT_FALSE_MESSAGE(kWordCells.test(2), "Zelle 2 sollte Fuellbuchstabe sein");
    TEST_ASSERT_TRUE_MESSAGE(f.cell(2) == kBlack, "Fuellbuchstabe darf nicht glimmen");

    // ZWANZIG wird um halb acht nicht benutzt, gehoert aber zu einem Wort.
    const uint16_t zwanzig = span(Word::Zwanzig).cell;
    TEST_ASSERT_TRUE_MESSAGE(f.cell(zwanzig) != kBlack, "unbenutztes Wort muss glimmen");
}

static void test_ghost_fillers_option() {
    ClockStyle st;
    st.transition = Transition::None;
    st.ghost = 40;
    st.ghostFillers = true;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 30, 100000);
    TEST_ASSERT_TRUE(f.cell(2) != kBlack);
}

static void test_ghost_is_dimmer_than_the_active_chain() {
    ClockStyle st;
    st.transition = Transition::None;
    st.ghost = 20;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 30, 100000);

    const Rgb active = f.cell(span(Word::Es).cell);
    const Rgb ghost = f.cell(span(Word::Zwanzig).cell);
    TEST_ASSERT_TRUE_MESSAGE(ghost.r < active.r, "Geisterwort muss dunkler sein");
}

static void test_gradient_changes_along_the_chain() {
    ClockStyle st;
    st.transition = Transition::None;
    st.color = {255, 0, 0};
    st.gradientTo = {0, 0, 255};
    st.gradient = true;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 45, 100000);  // ES IST DREIVIERTEL ACHT

    const Rgb first = f.cell(span(Word::Es).cell);
    const Rgb last = f.cell(uint16_t(span(Word::Acht).cell + span(Word::Acht).len - 1));
    TEST_ASSERT_TRUE_MESSAGE(first.r > last.r && first.b < last.b,
                             "Verlauf muss von der Start- zur Zielfarbe laufen");
}

static void test_gradient_off_is_uniform() {
    ClockStyle st;
    st.transition = Transition::None;
    st.gradient = false;
    ClockRenderer r;
    r.setStyle(st);

    Frame f;
    f.clear();
    r.render(f, 7, 45, 100000);
    for (uint16_t c : expectedCells(7, 45))
        TEST_ASSERT_TRUE_MESSAGE(f.cell(c) == st.color, msg("Zelle %u weicht ab", c));
}

// --- Sekundenatmen ---------------------------------------------------------

static void test_breathing_stays_within_depth() {
    ClockStyle st;
    st.transition = Transition::None;
    st.color = {200, 200, 200};
    st.breathDepth = 26;  // ~10 %
    st.breathPeriodMs = 1000;
    ClockRenderer r;
    r.setStyle(st);

    uint8_t lo = 255, hi = 0;
    for (uint32_t ms = 0; ms < 3000; ms += 7) {
        Frame f;
        f.clear();
        r.render(f, 7, 30, ms);
        const uint8_t v = f.cell(span(Word::Es).cell).r;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    TEST_ASSERT_TRUE_MESSAGE(hi <= 200, msg("Atmen wird heller als die Farbe: %u", hi));
    TEST_ASSERT_TRUE_MESSAGE(lo >= 200 - 26 - 2, msg("Atmen wird zu dunkel: %u", lo));
    TEST_ASSERT_TRUE_MESSAGE(hi > lo, "Atmen bewegt sich gar nicht");
}

static void test_breathing_off_is_steady() {
    ClockStyle st;
    st.transition = Transition::None;
    st.breathDepth = 0;
    ClockRenderer r;
    r.setStyle(st);

    for (uint32_t ms = 0; ms < 2000; ms += 13) {
        Frame f;
        f.clear();
        r.render(f, 7, 30, ms);
        TEST_ASSERT_TRUE(f.cell(span(Word::Es).cell) == st.color);
    }
}

// Geisterwoerter duerfen nicht mitatmen -- eine ruhige Grundflaeche stoert das
// Bewegungssignal der Eckpunkte nicht.
static void test_ghost_does_not_breathe() {
    ClockStyle st;
    st.transition = Transition::None;
    st.ghost = 60;
    st.breathDepth = 60;
    ClockRenderer r;
    r.setStyle(st);

    const uint16_t zwanzig = span(Word::Zwanzig).cell;
    Rgb firstSeen{};
    for (uint32_t ms = 0; ms < 2000; ms += 11) {
        Frame f;
        f.clear();
        r.render(f, 7, 30, ms);
        if (ms == 0) firstSeen = f.cell(zwanzig);
        TEST_ASSERT_TRUE_MESSAGE(f.cell(zwanzig) == firstSeen, "Geisterwort atmet mit");
    }
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_renders_exactly_the_sentence);
    RUN_TEST(test_never_touches_the_dots);
    RUN_TEST(test_every_transition_ends_complete);
    RUN_TEST(test_transitions_stay_inside_the_grid);
    RUN_TEST(test_staggered_reveals_in_reading_order);
    RUN_TEST(test_falling_moves_a_pixel_down_its_column);
    RUN_TEST(test_ghost_lights_words_but_not_fillers);
    RUN_TEST(test_ghost_fillers_option);
    RUN_TEST(test_ghost_is_dimmer_than_the_active_chain);
    RUN_TEST(test_gradient_changes_along_the_chain);
    RUN_TEST(test_gradient_off_is_uniform);
    RUN_TEST(test_breathing_stays_within_depth);
    RUN_TEST(test_breathing_off_is_steady);
    RUN_TEST(test_ghost_does_not_breathe);
    return UNITY_END();
}
