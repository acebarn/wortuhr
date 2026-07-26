// Host-Tests fuer den Wortuhr-Kern. Keine Hardware noetig.
//
//   pio test -e native

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "wordclock/Geometry.h"
#include "wordclock/TimeToWords.h"
#include "wordclock/WordLayout.h"

using namespace wordclock;

void setUp(void) {}
void tearDown(void) {}

// --- Hilfen ----------------------------------------------------------------

// Unity-Meldungen sind const char*; hier formatiert in einen statischen Puffer.
static char g_msg[256];

static const char* msg(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_msg, sizeof(g_msg), fmt, args);
    va_end(args);
    return g_msg;
}

static std::string render(const Sentence& s) {
    std::string out;
    for (uint8_t i = 0; i < s.len; ++i) {
        if (i) out += ' ';
        out += text(s.words[i]);
    }
    return out;
}

// =============================================================================

static void test_grid_has_expected_size() {
    TEST_ASSERT_EQUAL_UINT(kLetterCount, std::strlen(kGrid));
    TEST_ASSERT_EQUAL_UINT(110, kLetterCount);
    TEST_ASSERT_EQUAL_UINT(114, kLedCount);
}

static void test_word_offsets_match_grid() {
    for (uint8_t i = 0; i < kWordCount; ++i) {
        const Word w = Word(i);
        const WordSpan sp = span(w);
        const char* expected = text(w);

        TEST_ASSERT_TRUE_MESSAGE(sp.cell + sp.len <= kLetterCount,
                                 msg("%s ragt ueber das Raster hinaus", expected));
        TEST_ASSERT_EQUAL_UINT_MESSAGE(sp.len, std::strlen(expected),
                                       msg("%s: Laenge passt nicht zum Span", expected));

        const std::string actual(kGrid + sp.cell, sp.len);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            expected, actual.c_str(),
            msg("Rasterposition %u ergibt nicht \"%s\"", unsigned(sp.cell), expected));
    }
}

static void test_geometry_is_bijective() {
    std::set<uint16_t> seen;
    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const uint16_t idx = letterIndex(x, y);
            TEST_ASSERT_TRUE_MESSAGE(idx < kLetterCount,
                                     msg("(%u,%u) -> %u liegt ausserhalb", x, y, idx));
            TEST_ASSERT_TRUE_MESSAGE(seen.insert(idx).second,
                                     msg("(%u,%u) -> %u doppelt vergeben", x, y, idx));
        }
    }
    TEST_ASSERT_EQUAL_UINT(kLetterCount, seen.size());
}

static void test_serpentine_starts_top_right() {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, letterIndex(10, 0), "oben rechts muss Index 0 sein");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(10, letterIndex(0, 0), "Zeile 0 laeuft rechts nach links");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(11, letterIndex(0, 1), "Zeile 1 laeuft links nach rechts");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(21, letterIndex(10, 1), "Zeile 1 endet bei 21");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(22, letterIndex(10, 2), "Zeile 2 laeuft wieder rueckwaerts");
}

static void test_dots_do_not_collide_with_grid() {
    std::set<uint16_t> dots;
    for (uint8_t i = 0; i < kDotCount; ++i) {
        const uint16_t idx = dotIndex(i);
        TEST_ASSERT_TRUE_MESSAGE(idx >= kLetterCount,
                                 msg("Punkt %u hat Index %u -- das ist eine Rasterzelle", i, idx));
        TEST_ASSERT_TRUE_MESSAGE(idx < kLedCount,
                                 msg("Punkt %u hat Index %u ausserhalb des Strips", i, idx));
        TEST_ASSERT_TRUE_MESSAGE(dots.insert(idx).second, msg("Punkt-Index %u doppelt", idx));
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(113, dotIndex(0),
                                   "erster Minutenpunkt muss 113 sein (Altfirmware-Reihenfolge)");
}

// Ein Wortuhr-Satz muss in Leserichtung lesbar sein: jedes Wort muss im Raster
// hinter dem vorigen beginnen und darf es nicht ueberlappen. Im Altprojekt ergab
// sich das zufaellig aus der Vorwaertssuche -- hier wird es geprueft.
static void test_every_minute_reads_forward() {
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const Sentence s = timeToWords(uint8_t(h), uint8_t(m));

            TEST_ASSERT_TRUE_MESSAGE(s.len >= 3, msg("%02d:%02d ergibt nur %u Woerter", h, m,
                                                     unsigned(s.len)));
            TEST_ASSERT_TRUE_MESSAGE(s.words[0] == Word::Es && s.words[1] == Word::Ist,
                                     msg("%02d:%02d beginnt nicht mit ES IST", h, m));

            uint16_t cursor = 0;
            for (uint8_t i = 0; i < s.len; ++i) {
                const WordSpan sp = span(s.words[i]);
                TEST_ASSERT_TRUE_MESSAGE(
                    sp.cell >= cursor,
                    msg("%02d:%02d \"%s\": %s beginnt bei %u, vorheriges endet bei %u", h, m,
                        render(s).c_str(), text(s.words[i]), unsigned(sp.cell), unsigned(cursor)));
                cursor = uint16_t(sp.cell + sp.len);
                TEST_ASSERT_TRUE_MESSAGE(
                    cursor <= kLetterCount,
                    msg("%02d:%02d: %s ragt aus dem Raster", h, m, text(s.words[i])));
            }

            // 4 Punkte sind gueltig -- dann leuchten alle vier.
            TEST_ASSERT_TRUE(minuteDots(uint8_t(m)) <= kDotCount);
        }
    }
}

static void test_known_times() {
    struct Expect {
        uint8_t h, m;
        const char* text;
    };

    static const Expect kExpected[] = {
        {0, 0, "ES IST ZWOLF UHR"},
        {1, 0, "ES IST EIN UHR"},
        {1, 5, "ES IST FUNF NACH EINS"},
        {7, 15, "ES IST VIERTEL NACH SIEBEN"},
        {7, 20, "ES IST ZEHN VOR HALB ACHT"},
        {7, 25, "ES IST FUNF VOR HALB ACHT"},
        {7, 30, "ES IST HALB ACHT"},
        {7, 35, "ES IST FUNF NACH HALB ACHT"},
        {7, 40, "ES IST ZEHN NACH HALB ACHT"},
        {7, 45, "ES IST DREIVIERTEL ACHT"},
        {7, 50, "ES IST ZEHN VOR ACHT"},
        {7, 55, "ES IST FUNF VOR ACHT"},
        {12, 0, "ES IST ZWOLF UHR"},
        {12, 45, "ES IST DREIVIERTEL EINS"},
        {13, 0, "ES IST EIN UHR"},
        {20, 59, "ES IST FUNF VOR NEUN"},
        {23, 45, "ES IST DREIVIERTEL ZWOLF"},
        {23, 59, "ES IST FUNF VOR ZWOLF"},
    };

    for (const Expect& e : kExpected) {
        const std::string got = render(timeToWords(e.h, e.m));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.text, got.c_str(),
                                         msg("bei %02u:%02u", e.h, e.m));
    }
}

static void test_ein_only_with_uhr() {
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const Sentence s = timeToWords(uint8_t(h), uint8_t(m));
            bool hasEin = false, hasUhr = false;
            for (uint8_t i = 0; i < s.len; ++i) {
                if (s.words[i] == Word::Ein) hasEin = true;
                if (s.words[i] == Word::Uhr) hasUhr = true;
            }
            if (hasEin) {
                TEST_ASSERT_TRUE_MESSAGE(
                    hasUhr, msg("%02d:%02d nutzt EIN ohne UHR: \"%s\"", h, m, render(s).c_str()));
            }
        }
    }
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_grid_has_expected_size);
    RUN_TEST(test_word_offsets_match_grid);
    RUN_TEST(test_geometry_is_bijective);
    RUN_TEST(test_serpentine_starts_top_right);
    RUN_TEST(test_dots_do_not_collide_with_grid);
    RUN_TEST(test_every_minute_reads_forward);
    RUN_TEST(test_known_times);
    RUN_TEST(test_ein_only_with_uhr);
    return UNITY_END();
}
