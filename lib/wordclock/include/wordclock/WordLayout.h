#pragma once

#include <cstdint>

#include "wordclock/Geometry.h"

// Das Buchstabenraster der Frontplatte und die Woerter darin.
//
// Woerter sind Konstanten mit festem Offset, KEINE Laufzeitsuche im Rastertext.
// Das Altprojekt suchte jedes Wort per indexOf() vorwaerts ab der Position des
// vorigen Treffers -- das funktionierte nur, solange die Wortreihenfolge im
// Satz zufaellig mit der Rasterreihenfolge uebereinstimmte, und scheiterte
// stumm (Rueckgabe -1, Anzeige bleibt leer), wenn nicht.
//
// Die Offsets hier werden von test_wordlogic gegen kGrid geprueft.

namespace wordclock {

// Rastertext in Leserichtung, zeilenweise. Genau kLetterCount Zeichen.
inline constexpr const char* kGrid =
    "ESKISTAFUNF"   // 0   ES, IST, FUNF
    "ZEHNZWANZIG"   // 11  ZEHN, ZWANZIG
    "DREIVIERTEL"   // 22  DREIVIERTEL, VIERTEL
    "VORDIRSNACH"   // 33  VOR, NACH
    "HALBAELFUNF"   // 44  HALB, ELF, FUNF
    "EINSXAMZWEI"   // 55  EIN(S), ZWEI
    "DREIAUJVIER"   // 66  DREI, VIER
    "SECHSNLACHT"   // 77  SECHS, ACHT
    "SIEBENZWOLF"   // 88  SIEBEN, ZWOLF
    "ZEHNEUNKUHR";  // 99  ZEHN, NEUN, UHR

enum class Word : uint8_t {
    Es,
    Ist,
    FuenfMin,     // Zeile 0 -- Minutenangabe
    ZehnMin,      // Zeile 1 -- Minutenangabe
    Zwanzig,      // im Raster vorhanden, im Dialekt ungenutzt
    Dreiviertel,  // :45
    Viertel,      // :15, liegt in DREIVIERTEL
    Vor,
    Nach,
    Halb,
    Elf,
    FuenfStd,  // Zeile 4 -- Stundenangabe
    Ein,       // "EIN UHR"
    Eins,      // "EINS" ab Minute 5
    Zwei,
    Drei,
    Vier,
    Sechs,
    Acht,
    Sieben,
    Zwoelf,
    ZehnStd,  // Zeile 9 -- Stundenangabe
    Neun,
    Uhr,
    Count
};

inline constexpr uint8_t kWordCount = uint8_t(Word::Count);

struct WordSpan {
    uint16_t cell;  // Startposition im Rastertext (Leserichtung)
    uint8_t len;
};

// Reihenfolge muss zu enum Word passen.
inline constexpr WordSpan kWordSpan[kWordCount] = {
    {0, 2},     // Es
    {3, 3},     // Ist
    {7, 4},     // FuenfMin
    {11, 4},    // ZehnMin
    {15, 7},    // Zwanzig
    {22, 11},   // Dreiviertel
    {26, 7},    // Viertel
    {33, 3},    // Vor
    {40, 4},    // Nach
    {44, 4},    // Halb
    {49, 3},    // Elf
    {51, 4},    // FuenfStd   -- teilt sich das F an 51 mit Elf
    {55, 3},    // Ein
    {55, 4},    // Eins
    {62, 4},    // Zwei
    {66, 4},    // Drei
    {73, 4},    // Vier
    {77, 5},    // Sechs
    {84, 4},    // Acht
    {88, 6},    // Sieben
    {94, 5},    // Zwoelf
    {99, 4},    // ZehnStd
    {102, 4},   // Neun       -- teilt sich das N an 102 mit ZehnStd
    {107, 3},   // Uhr
};

inline constexpr const char* kWordText[kWordCount] = {
    "ES",   "IST",  "FUNF",  "ZEHN",   "ZWANZIG", "DREIVIERTEL",
    "VIERTEL", "VOR", "NACH", "HALB",  "ELF",     "FUNF",
    "EIN",  "EINS", "ZWEI",  "DREI",   "VIER",    "SECHS",
    "ACHT", "SIEBEN", "ZWOLF", "ZEHN", "NEUN",    "UHR",
};

constexpr WordSpan span(Word w) { return kWordSpan[uint8_t(w)]; }
constexpr const char* text(Word w) { return kWordText[uint8_t(w)]; }

// Welche Rasterzellen gehoeren ueberhaupt zu einem Wort?
//
// Das Raster enthaelt 15 Fuellbuchstaben (K, A, DIRS, X, AM, AUJ, NL, K), die
// zu keinem Wort gehoeren. Die Geisterwoerter-Darstellung laesst sie per
// Vorgabe dunkel, damit die Frontplatte gegliedert wirkt statt gleichmaessig
// zu glimmen. 14 Byte, zur Uebersetzungszeit berechnet.
struct WordCellMask {
    uint8_t bits[(kLetterCount + 7) / 8] = {};
    constexpr bool test(uint16_t cell) const {
        return cell < kLetterCount && ((bits[cell / 8] >> (cell % 8)) & 1);
    }
};

constexpr WordCellMask makeWordCellMask() {
    WordCellMask m{};
    for (uint8_t w = 0; w < kWordCount; ++w) {
        for (uint8_t i = 0; i < kWordSpan[w].len; ++i) {
            const uint16_t c = uint16_t(kWordSpan[w].cell + i);
            m.bits[c / 8] = uint8_t(m.bits[c / 8] | (1u << (c % 8)));
        }
    }
    return m;
}

inline constexpr WordCellMask kWordCells = makeWordCellMask();

}  // namespace wordclock
