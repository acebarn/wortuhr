// Host-Tests fuer den Wortuhr-Kern. Keine Hardware noetig.
//
//   pio test -e native
//   oder direkt:
//   clang++ -std=c++17 -Ilib/wordclock/include lib/wordclock/src/*.cpp \
//           test/test_wordlogic/main.cpp -o /tmp/t && /tmp/t

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "wordclock/Geometry.h"
#include "wordclock/TimeToWords.h"
#include "wordclock/WordLayout.h"

using namespace wordclock;

// --- winziges Test-Geruest -------------------------------------------------

static int g_failures = 0;
static int g_checks = 0;
static const char* g_case = "";

static void testCase(const char* name) {
    g_case = name;
    std::printf("\n  %s\n", name);
}

#define CHECK(cond, ...)                                       \
    do {                                                       \
        ++g_checks;                                            \
        if (!(cond)) {                                         \
            ++g_failures;                                      \
            std::printf("    FEHLER  " __VA_ARGS__);           \
            std::printf("\n            %s:%d\n", __FILE__, __LINE__); \
        }                                                      \
    } while (0)

// --- Hilfen ----------------------------------------------------------------

static std::string render(const Sentence& s) {
    std::string out;
    for (uint8_t i = 0; i < s.len; ++i) {
        if (i) out += ' ';
        out += text(s.words[i]);
    }
    return out;
}

// =============================================================================

static void testGrid() {
    testCase("Rastertext");

    CHECK(std::strlen(kGrid) == kLetterCount, "Raster hat %zu Zeichen, erwartet %u",
          std::strlen(kGrid), unsigned(kLetterCount));
    CHECK(kLetterCount == 110, "kLetterCount = %u", unsigned(kLetterCount));
    CHECK(kLedCount == 114, "kLedCount = %u", unsigned(kLedCount));
}

static void testWordOffsets() {
    testCase("Wortkonstanten stimmen mit dem Raster ueberein");

    for (uint8_t i = 0; i < kWordCount; ++i) {
        const Word w = Word(i);
        const WordSpan sp = span(w);
        const char* expected = text(w);

        CHECK(sp.cell + sp.len <= kLetterCount, "%s ragt ueber das Raster hinaus", expected);
        CHECK(std::strlen(expected) == sp.len, "%s: Laenge %zu, Span sagt %u", expected,
              std::strlen(expected), unsigned(sp.len));

        if (sp.cell + sp.len <= kLetterCount) {
            const std::string actual(kGrid + sp.cell, sp.len);
            CHECK(actual == expected, "Position %u ergibt \"%s\", erwartet \"%s\"",
                  unsigned(sp.cell), actual.c_str(), expected);
        }
    }
}

static void testGeometryIsBijective() {
    testCase("Rasterabbildung ist eineindeutig");

    std::set<uint16_t> seen;
    for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
            const uint16_t idx = letterIndex(x, y);
            CHECK(idx < kLetterCount, "(%u,%u) -> %u liegt ausserhalb", x, y, idx);
            CHECK(seen.insert(idx).second, "(%u,%u) -> %u doppelt vergeben", x, y, idx);
        }
    }
    CHECK(seen.size() == kLetterCount, "%zu von %u Indizes belegt", seen.size(),
          unsigned(kLetterCount));

    // Serpentine, erste LED oben rechts
    CHECK(letterIndex(10, 0) == 0, "oben rechts muss Index 0 sein, ist %u", letterIndex(10, 0));
    CHECK(letterIndex(0, 0) == 10, "Zeile 0 laeuft rechts nach links");
    CHECK(letterIndex(0, 1) == 11, "Zeile 1 laeuft links nach rechts");
    CHECK(letterIndex(10, 1) == 21, "Zeile 1 endet bei 21");
    CHECK(letterIndex(10, 2) == 22, "Zeile 2 laeuft wieder rechts nach links");
}

static void testDotsAreSeparate() {
    testCase("Minutenpunkte kollidieren nicht mit dem Raster");

    std::set<uint16_t> dots;
    for (uint8_t i = 0; i < kDotCount; ++i) {
        const uint16_t idx = dotIndex(i);
        CHECK(idx >= kLetterCount, "Punkt %u hat Index %u -- das ist eine Rasterzelle", i, idx);
        CHECK(idx < kLedCount, "Punkt %u hat Index %u ausserhalb des Strips", i, idx);
        CHECK(dots.insert(idx).second, "Punkt-Index %u doppelt", idx);
    }
    CHECK(dotIndex(0) == 113, "erster Minutenpunkt muss 113 sein (Altfirmware-Reihenfolge)");
}

static void testEveryMinuteOfTheDay() {
    testCase("Alle 1440 Minuten des Tages");

    int checkedSentences = 0;

    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const Sentence s = timeToWords(uint8_t(h), uint8_t(m));
            ++checkedSentences;

            if (s.len < 3) {
                CHECK(false, "%02d:%02d ergibt nur %u Woerter", h, m, unsigned(s.len));
                continue;
            }

            CHECK(s.words[0] == Word::Es && s.words[1] == Word::Ist,
                  "%02d:%02d beginnt nicht mit ES IST", h, m);

            // Ein Wortuhr-Satz muss in Leserichtung lesbar sein: jedes Wort
            // muss im Raster hinter dem vorigen beginnen und darf es nicht
            // ueberlappen. Im Altprojekt ergab sich das zufaellig aus der
            // Vorwaertssuche -- hier wird es geprueft.
            uint16_t cursor = 0;
            for (uint8_t i = 0; i < s.len; ++i) {
                const WordSpan sp = span(s.words[i]);
                if (sp.cell < cursor) {
                    CHECK(false, "%02d:%02d \"%s\": %s beginnt bei %u, vorheriges endet bei %u",
                          h, m, render(s).c_str(), text(s.words[i]), unsigned(sp.cell),
                          unsigned(cursor));
                    break;
                }
                cursor = uint16_t(sp.cell + sp.len);
                CHECK(cursor <= kLetterCount, "%02d:%02d: %s ragt aus dem Raster", h, m,
                      text(s.words[i]));
            }

            // 4 Punkte sind gueltig -- dann leuchten alle vier.
            const uint8_t dots = minuteDots(uint8_t(m));
            CHECK(dots <= kDotCount, "%02d:%02d fordert %u Minutenpunkte", h, m, unsigned(dots));
        }
    }

    std::printf("    %d Saetze geprueft\n", checkedSentences);
}

static void testKnownTimes() {
    testCase("Bekannte Uhrzeiten");

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
        CHECK(got == e.text, "%02u:%02u ergibt \"%s\", erwartet \"%s\"", e.h, e.m, got.c_str(),
              e.text);
    }
}

static void testEinsVsEin() {
    testCase("EIN nur zusammen mit UHR");

    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const Sentence s = timeToWords(uint8_t(h), uint8_t(m));
            bool hasEin = false, hasUhr = false;
            for (uint8_t i = 0; i < s.len; ++i) {
                if (s.words[i] == Word::Ein) hasEin = true;
                if (s.words[i] == Word::Uhr) hasUhr = true;
            }
            if (hasEin) {
                CHECK(hasUhr, "%02d:%02d nutzt EIN ohne UHR: \"%s\"", h, m, render(s).c_str());
            }
        }
    }
}

// =============================================================================

int main() {
    std::printf("Wortuhr — Kern-Tests");

    testGrid();
    testWordOffsets();
    testGeometryIsBijective();
    testDotsAreSeparate();
    testEveryMinuteOfTheDay();
    testKnownTimes();
    testEinsVsEin();

    std::printf("\n%s  %d Pruefungen, %d Fehler\n\n", g_failures ? "FEHLGESCHLAGEN" : "BESTANDEN",
                g_checks, g_failures);
    return g_failures ? 1 : 0;
}
