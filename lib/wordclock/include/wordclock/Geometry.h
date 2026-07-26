#pragma once

#include <cstdint>

// Die Panel-Geometrie. Einzige Quelle der Wahrheit.
//
// Im Altprojekt war sie als Praeprozessor-Makro ueber drei Header verstreut
// (ledmatrix.h HEIGHT=11, tetris.h HEIGHT=10, Haupt-Sketch HEIGHT=11). Die
// widersprechenden Werte haben mehrere Fehler verursacht, die jahrelang
// unbemerkt blieben. Hier gibt es die Werte genau einmal, und nichts ausser
// diesem Header darf ueber Strip-Indizes Bescheid wissen.

namespace wordclock {

// --- Buchstabenraster ------------------------------------------------------

inline constexpr uint8_t kWidth = 11;
inline constexpr uint8_t kHeight = 10;
inline constexpr uint16_t kLetterCount = uint16_t(kWidth) * kHeight;  // 110

// --- Minutenpunkte ---------------------------------------------------------
//
// Vier LEDs in den Ecken der Frontplatte, elektrisch die Fortsetzung des
// Strips nach dem letzten Buchstaben. Sie sind KEINE elfte Rasterzeile --
// genau diese Verwechslung war der Kern des Altprojekt-Problems.

inline constexpr uint8_t kDotCount = 4;
inline constexpr uint16_t kLedCount = kLetterCount + kDotCount;  // 114

// Reihenfolge des Zuschaltens fuer die Minuten 1..4, uebernommen aus der
// Altfirmware (dort: Zeile 10, x = 7..10 bei serpentiner Zaehlung).
//
// TODO: Welcher Index physisch in welcher Ecke sitzt, ist nicht dokumentiert
//       und muss einmal am Geraet ausgemessen werden. Fuer die Minutenanzeige
//       ist nur die Reihenfolge relevant, fuer die Fehlercodes spaeter auch
//       die Position.
inline constexpr uint16_t kDotIndex[kDotCount] = {113, 112, 111, 110};

// --- Abbildung Raster -> Strip ---------------------------------------------
//
// Serpentine, zeilenweise, erste LED oben rechts:
//
//   Zeile 0   <-------  Index  0.. 10   (x = 10 .. 0)
//   Zeile 1   ------->  Index 11.. 21   (x =  0 .. 10)
//   Zeile 2   <-------  Index 22.. 32
//   ...
//
// Entspricht NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_ROWS +
// NEO_MATRIX_ZIGZAG der Adafruit-Bibliothek.

constexpr uint16_t letterIndex(uint8_t x, uint8_t y) {
    return (y % 2 == 0) ? uint16_t(y) * kWidth + (kWidth - 1 - x)
                        : uint16_t(y) * kWidth + x;
}

// Position im flachen Rastertext (zeilenweise, links nach rechts) -> Strip.
// Der Rastertext ist Leserichtung, der Strip ist Serpentine; die beiden
// stimmen nur in geraden Zeilen zufaellig ueberein.
constexpr uint16_t letterIndexFromCell(uint16_t cell) {
    return letterIndex(uint8_t(cell % kWidth), uint8_t(cell / kWidth));
}

constexpr uint16_t dotIndex(uint8_t dot) { return kDotIndex[dot]; }

constexpr bool inBounds(uint8_t x, uint8_t y) { return x < kWidth && y < kHeight; }

}  // namespace wordclock
