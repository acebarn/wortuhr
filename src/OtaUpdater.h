#pragma once

#include <ArduinoOTA.h>

#include "wordclock/Frame.h"
#include "wordclock/Ports.h"

// Firmware-Update ueber WLAN (DESIGN 10).
//
// Geraetesache, deshalb hier und nicht in lib/wordclock: der Simulator hat
// nichts zu flashen, und die portable Anwendung soll nichts von ArduinoOTA
// wissen muessen.
//
// Drei Entscheidungen stecken darin:
//
// Ohne Kennwort kein OTA. Ein offener Update-Dienst im Heimnetz heisst, dass
// jeder Gast die Uhr umflashen kann; ein voreingestelltes Kennwort waere
// dasselbe in Gruen. Steht in den Zugangsdaten nichts, meldet sich der Dienst
// gar nicht erst an.
//
// Der Fortschritt gehoert auf die Frontplatte. Waehrend des Updates laeuft
// weder Webapp noch MQTT -- das Geraet haengt in ArduinoOTA.handle() fest, und
// wer davor steht, saehe sonst eine tote Uhr und zoege im Zweifel den Stecker.
// Genau das darf man mitten im Schreiben des Flash nicht tun.
//
// Gezeichnet wird nur bei jedem zwanzigstel Fortschritt. Die Datenleitung ist
// Bit-Bang und sperrt je Bild rund 3,4 ms die Interrupts (siehe
// StripAdapter.h); bei jedem Paket zu zeichnen wuerde die Uebertragung
// ausbremsen und im schlimmsten Fall abreissen lassen.
class OtaUpdater {
public:
    void begin(wordclock::IStrip* strip) { strip_ = strip; }

    // `password` kommt aus den Zugangsdaten und kann sich zur Laufzeit aendern
    // -- die Webapp darf es setzen, ohne dass jemand neu startet.
    void poll(bool wifiConnected, const char* password) {
        if (!started_) {
            if (!wifiConnected || !password || !*password) return;
            start(password);
            return;
        }
        ArduinoOTA.handle();
    }

    bool running() const { return running_; }

private:
    void start(const char* password) {
        ArduinoOTA.setHostname("wortuhr");
        ArduinoOTA.setPassword(password);
        // Kein Rebootzwang bei Fehlschlag: die Uhr soll weiterlaufen, wenn ein
        // Update scheitert. Das alte Abbild ist noch vollstaendig da.
        ArduinoOTA.setRebootOnSuccess(true);

        ArduinoOTA.onStart([this]() {
            running_ = true;
            lastStep_ = 0xFF;
            Serial.println("[ota] Update beginnt");
            draw(0, kAmber);
        });
        ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
            if (!total) return;
            const uint8_t step = uint8_t(uint32_t(done) * 20 / total);
            if (step == lastStep_) return;
            lastStep_ = step;
            draw(uint8_t(uint32_t(done) * 255 / total), kAmber);
        });
        ArduinoOTA.onEnd([this]() {
            Serial.println("[ota] fertig, Neustart");
            draw(255, kGreen);
        });
        ArduinoOTA.onError([this](ota_error_t error) {
            running_ = false;
            Serial.printf("[ota] Fehler %u\n", unsigned(error));
            draw(255, kRed);
        });

        ArduinoOTA.begin();
        started_ = true;
        Serial.println("[ota] bereit auf wortuhr.local");
    }

    // Ein Balken ueber das Buchstabenfeld, in Leserichtung gefuellt. Die
    // Eckpunkte bleiben frei -- sie gehoeren dem Gesundheitskanal (DESIGN 4),
    // und wer sie kennt, soll sie waehrend eines Updates nicht falsch deuten.
    void draw(uint8_t progress, wordclock::Rgb color) {
        if (!strip_) return;
        wordclock::Frame f;
        f.clear();
        const uint16_t lit = uint16_t(uint32_t(progress) * wordclock::kLetterCount / 255);
        for (uint16_t c = 0; c < lit; ++c) f.setCell(c, color);
        strip_->show(f);
    }

    static constexpr wordclock::Rgb kAmber{255, 180, 60};
    static constexpr wordclock::Rgb kGreen{0, 255, 90};
    static constexpr wordclock::Rgb kRed{255, 0, 0};

    wordclock::IStrip* strip_ = nullptr;
    bool started_ = false;
    bool running_ = false;
    uint8_t lastStep_ = 0xFF;
};
