#include "wordclock/TimeToWords.h"

namespace wordclock {
namespace {

// Stundenwort. `withUhr` unterscheidet EIN von EINS.
Word hourWord(uint8_t hour12, bool withUhr) {
    switch (hour12) {
        case 0:  return Word::Zwoelf;
        case 1:  return withUhr ? Word::Ein : Word::Eins;
        case 2:  return Word::Zwei;
        case 3:  return Word::Drei;
        case 4:  return Word::Vier;
        case 5:  return Word::FuenfStd;
        case 6:  return Word::Sechs;
        case 7:  return Word::Sieben;
        case 8:  return Word::Acht;
        case 9:  return Word::Neun;
        case 10: return Word::ZehnStd;
        default: return Word::Elf;
    }
}

}  // namespace

Sentence timeToWords(uint8_t hours, uint8_t minutes) {
    Sentence s;
    s.push(Word::Es);
    s.push(Word::Ist);

    const uint8_t bucket = minutes / 5;  // 0..11

    switch (bucket) {
        case 0:  break;                                                       // volle Stunde
        case 1:  s.push(Word::FuenfMin); s.push(Word::Nach); break;           // :05
        case 2:  s.push(Word::ZehnMin);  s.push(Word::Nach); break;           // :10
        case 3:  s.push(Word::Viertel);  s.push(Word::Nach); break;           // :15
        case 4:  s.push(Word::Zwanzig);  s.push(Word::Nach); break;           // :20
        case 5:  s.push(Word::FuenfMin); s.push(Word::Vor); s.push(Word::Halb); break;   // :25
        case 6:  s.push(Word::Halb); break;                                              // :30
        case 7:  s.push(Word::FuenfMin); s.push(Word::Nach); s.push(Word::Halb); break;  // :35
        case 8:  s.push(Word::Zwanzig);  s.push(Word::Vor); break;            // :40
        case 9:  s.push(Word::Dreiviertel); break;                                       // :45
        case 10: s.push(Word::ZehnMin);  s.push(Word::Vor); break;            // :50
        default: s.push(Word::FuenfMin); s.push(Word::Vor); break;            // :55
    }

    // Ab :25 bezieht sich die Angabe auf die kommende Stunde ("fuenf vor halb
    // acht" = 19:25). Bei :20 nicht -- "zwanzig nach sieben" meint die
    // laufende Stunde. Genau an dieser Grenze liegt der Unterschied zur
    // frueheren halb-bezogenen Formulierung.
    uint8_t hour12 = hours % 12;
    if (bucket >= 5) hour12 = uint8_t((hour12 + 1) % 12);

    const bool withUhr = (bucket == 0);
    s.push(hourWord(hour12, withUhr));
    if (withUhr) s.push(Word::Uhr);

    return s;
}

}  // namespace wordclock
