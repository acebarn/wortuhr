#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

// Verbindungsaufbau als nicht blockierender Zustandsautomat (DESIGN 9.1).
//
// setup() erreicht IMMER loop(). Das Altprojekt rief wifiManager.autoConnect()
// blockierend in setup() -- war beim Einschalten kein WLAN da, wurde loop() nie
// erreicht und die Uhr zeigte gar nichts mehr. Bei einem Stromausfall, nach dem
// der Router langsamer hochfaehrt als die Uhr, blieb sie so haengen.
//
// Der Wiederholungsabstand waechst, damit ein laenger abwesendes Netz nicht
// dauernd Funkverkehr erzeugt:
//
//   Versuch 1-3    alle 10 s
//   Versuch 4-6    alle 30 s
//   ab Versuch 7   alle 2 min
//
// NOCH NICHT UMGESETZT: der zusaetzliche Accesspoint ab Versuch 7. Er kommt
// zusammen mit der Konfigurations-Webapp -- ohne sie waere ein offener AP ohne
// Inhalt. Bis dahin meldet der Gesundheitskanal "kein WLAN" statt "AP-Modus".
class WifiConnector {
public:
    void begin(const char* ssid, const char* pass) {
        ssid_ = ssid;
        pass_ = pass;
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.hostname("wortuhr");
        attempt(millis());
    }

    void tick(uint32_t nowMs) {
        if (connected()) {
            attempts_ = 0;
            return;
        }
        if (int32_t(nowMs - nextAttemptMs_) >= 0) attempt(nowMs);
    }

    bool connected() const { return WiFi.status() == WL_CONNECTED; }
    uint8_t attempts() const { return attempts_; }

private:
    void attempt(uint32_t nowMs) {
        if (attempts_ < 255) ++attempts_;
        WiFi.begin(ssid_, pass_);

        uint32_t waitMs;
        if (attempts_ <= 3) waitMs = 10000;
        else if (attempts_ <= 6) waitMs = 30000;
        else waitMs = 120000;

        nextAttemptMs_ = nowMs + waitMs;
        Serial.printf("[wifi] Versuch %u, naechster in %u s\n", attempts_,
                      unsigned(waitMs / 1000));
    }

    const char* ssid_ = "";
    const char* pass_ = "";
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
};
