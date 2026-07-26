// Platzhalter. Der eigentliche Aufbau folgt gemaess DESIGN.md:
// Compositor, Notify-Stapel, Gesundheitsautomat, Verbindungsautomat, Adapter.
//
// Bis dahin bindet diese Datei den Kern ein, damit der Geraete-Build
// nachweist, dass lib/wordclock auch fuer Xtensa uebersetzt und linkt --
// nicht nur fuer den Host.

#include <Arduino.h>

#include "wordclock/Geometry.h"
#include "wordclock/TimeToWords.h"
#include "wordclock/WordLayout.h"

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.printf("Wortuhr - Panel %ux%u, %u LEDs\n", wordclock::kWidth, wordclock::kHeight,
                  wordclock::kLedCount);

    const wordclock::Sentence s = wordclock::timeToWords(7, 45);
    Serial.print("Testsatz: ");
    for (uint8_t i = 0; i < s.len; ++i) {
        Serial.print(wordclock::text(s.words[i]));
        Serial.print(' ');
    }
    Serial.println();
}

void loop() {
    delay(1000);
}
