#pragma once

#include <NeoPixelBus.h>

#include "wordclock/Frame.h"

// Ausgabe eines Frames auf den WS2812-Strip.
//
// Der einzige Ort, an dem ein Strip-Index die Firmware verlaesst. Frame haelt
// die Pixel bereits in Strip-Reihenfolge -- hier wird nur noch kopiert.
//
// VERFAHREN: Bit-Bang, weil die Datenleitung auf D1 (GPIO5) liegt.
// NeoPixelBus kann auf dem ESP8266 nur pinfest ohne Interrupt-Sperre ausgeben:
// DMA ausschliesslich auf GPIO3, UART1 ausschliesslich auf GPIO2. Auf jedem
// anderen Pin bleibt Bit-Bang, das die Interrupts fuer rund 3,4 ms je Bild
// sperrt. Die Altfirmware gab auf derselben Hardware ebenso aus und lief
// jahrelang stabil.
//
// Sollte es je flackern oder die WLAN-Verbindung darunter leiden, waere der
// Umbau auf GPIO3 die Loesung -- er kostet allerdings den seriellen Eingang.
class StripAdapter {
public:
    explicit StripAdapter(uint8_t pin) : strip_(wordclock::kLedCount, pin) {}

    void begin() {
        strip_.Begin();
        strip_.ClearTo(RgbColor(0));
        strip_.Show();
    }

    void show(const wordclock::Frame& f) {
        const wordclock::Rgb* px = f.strip();
        for (uint16_t i = 0; i < wordclock::kLedCount; ++i) {
            strip_.SetPixelColor(i, RgbColor(px[i].r, px[i].g, px[i].b));
        }
        strip_.Show();
    }

    // Nur fuer den Mapping-Test: eine einzelne LED ansteuern.
    void showSingle(uint16_t index, wordclock::Rgb color) {
        strip_.ClearTo(RgbColor(0));
        if (index < wordclock::kLedCount)
            strip_.SetPixelColor(index, RgbColor(color.r, color.g, color.b));
        strip_.Show();
    }

private:
    NeoPixelBus<NeoGrbFeature, NeoEsp8266BitBang800KbpsMethod> strip_;
};
