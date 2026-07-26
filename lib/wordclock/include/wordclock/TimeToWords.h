#pragma once

#include <cstdint>

#include "wordclock/WordLayout.h"

// Uhrzeit -> Wortkette.
//
// Dialekt (fest, weil in die Frontplatte geschnitten):
//   :45  DREIVIERTEL <naechste Stunde>   -- nicht "VIERTEL VOR"
//   :20  ZEHN VOR HALB <naechste>        -- nicht "ZWANZIG NACH"
//   :40  ZEHN NACH HALB <naechste>
//   Stunde 1 heisst "EIN" nur zusammen mit UHR, sonst "EINS".

namespace wordclock {

// Laengste Kette: "ES IST FUNF NACH HALB ZWOLF" = 6
inline constexpr uint8_t kMaxSentenceLen = 8;

struct Sentence {
    Word words[kMaxSentenceLen];
    uint8_t len = 0;

    void push(Word w) {
        if (len < kMaxSentenceLen) words[len++] = w;
    }
};

// hours: 0..23, minutes: 0..59
Sentence timeToWords(uint8_t hours, uint8_t minutes);

// Anzahl leuchtender Minutenpunkte: 0..4 (bei Minute % 5 == 4 leuchten alle vier).
constexpr uint8_t minuteDots(uint8_t minutes) { return minutes % 5; }

}  // namespace wordclock
