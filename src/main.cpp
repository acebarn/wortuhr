// Wortuhr — Verdrahtung der Geraeteadapter. Siehe DESIGN.md.
//
// Die Hauptschleife liegt in wordclock::App und ist portabel: Firmware und
// Simulator fuehren denselben Code aus, nur mit anderen Adaptern. Hier steht
// deshalb nichts als das Zusammenstecken.

#include <Arduino.h>

#if !__has_include("secrets.h")
#error "include/secrets_example.h nach include/secrets.h kopieren und ausfuellen."
#endif
#include "secrets.h"

#ifndef MQTT_HOST
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#endif

// Leer heisst: kein OTA. Siehe OtaUpdater.h.
#ifndef OTA_PASS
#define OTA_PASS ""
#endif

#include "DeviceAdapters.h"
#include "OtaUpdater.h"
#include "wordclock/App.h"

using namespace wordclock;

namespace {

constexpr uint8_t kDataPin = D1;  // GPIO5
constexpr uint16_t kStatusIntervalMs = 5000;

DeviceStrip strip(kDataPin);
DeviceClock deviceClock;
DeviceNetwork network;
DeviceStorage storage;
DeviceMqtt mqttTransport;
DeviceSystem systemInfo;
DeviceWeb web;
BootGuard bootGuard;
OtaUpdater ota;

Ports makePorts() {
    Ports p;
    p.strip = &strip;
    p.clock = &deviceClock;
    p.network = &network;
    p.storage = &storage;
    p.mqtt = &mqttTransport;
    p.system = &systemInfo;
    return p;
}

App app(makePorts());
uint32_t lastStatusMs = 0;

#ifdef DOT_MAPPING_TEST
// Beantwortet die beiden Fragen, die sich nur am Geraet klaeren lassen:
// Stimmt die angenommene Ausrichtung des Rasters, und welcher Strip-Index
// sitzt in welcher Ecke?
void runMappingTest() {
    for (uint16_t cycle = 1;; ++cycle) {
        Serial.printf("\n=== Mapping-Test, Durchlauf %u ===\n", cycle);

        Serial.println("1) Zeilen von OBEN nach UNTEN");
        for (uint8_t y = 0; y < kHeight; ++y) {
            Frame f;
            f.clear();
            for (uint8_t x = 0; x < kWidth; ++x) f.setXY(x, y, {0, 60, 0});
            strip.show(f);
            Serial.printf("   Zeile %u\n", y);
            delay(500);
        }

        Serial.println("2) Spalte 0 (LINKS), dann Spalte 10 (RECHTS)");
        for (uint8_t x : {uint8_t(0), uint8_t(kWidth - 1)}) {
            Frame f;
            f.clear();
            for (uint8_t y = 0; y < kHeight; ++y) f.setXY(x, y, {0, 0, 80});
            strip.show(f);
            Serial.printf("   Spalte %u -- %s erwartet\n", x, x == 0 ? "links" : "rechts");
            delay(2000);
        }

        Serial.println("3) Eckpunkte einzeln. NOTIEREN, welche Ecke leuchtet!");
        for (uint8_t d = 0; d < kDotCount; ++d) {
            strip.raw().showSingle(dotIndex(d), {120, 0, 0});
            Serial.printf("   >>> Punkt %u = Strip-Index %u  -- welche Ecke leuchtet jetzt?\n", d,
                          dotIndex(d));
            delay(4000);
        }

        Frame off;
        off.clear();
        strip.show(off);
        Serial.println("--- Pause, dann von vorn (Strom trennen zum Beenden) ---");
        delay(2000);
    }
}
#endif

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\n\nWortuhr — Panel %ux%u, %u LEDs, Pin D1\n", kWidth, kHeight, kLedCount);
    Serial.printf("Build: %s\n", __TIMESTAMP__);

    strip.begin();
    storage.begin();
    bootGuard.begin();

#ifdef DOT_MAPPING_TEST
    runMappingTest();
#endif

    // Ab hier wird NICHTS mehr blockiert: loop() wird immer erreicht, auch
    // ohne WLAN. Die Uhr bleibt unter allen Umstaenden eine Uhr (DESIGN 9.1).
    deviceClock.begin();
    mqttTransport.begin();

    // secrets.h ist nur Erstbefuellung: was einmal ueber die Webapp gesetzt
    // wurde, darf ein Neuflashen nicht zurueckdrehen.
    Secrets seed;
    seed.set(SecretKey::WifiSsid, WIFI_SSID);
    seed.set(SecretKey::WifiPass, WIFI_PASS);
    seed.set(SecretKey::MqttHost, MQTT_HOST);
    seed.set(SecretKey::MqttUser, MQTT_USER);
    seed.set(SecretKey::MqttPass, MQTT_PASS);
    seed.set(SecretKey::OtaPass, OTA_PASS);
    { char portBuf[8]; snprintf(portBuf, sizeof(portBuf), "%d", MQTT_PORT);
      seed.set(SecretKey::MqttPort, portBuf); }

    app.begin(&seed);
    web.begin(&app.web());
    ota.begin(&strip);

    if (bootGuard.triggered()) {
        Serial.println("[boot] fuenf abgebrochene Starts -> Werksreset");
        app.applyWebAction(WebAction::FactoryReset);
    }
    Serial.println("[setup] fertig, loop laeuft");
}

void loop() {
    const uint32_t nowMs = millis();

    network.poll();
    deviceClock.poll();

    // Vor allem anderen: waehrend eines Updates darf die Uhr weder zeichnen
    // noch senden. ArduinoOTA.handle() kehrt erst zurueck, wenn das Abbild
    // geschrieben ist -- der Rest der Schleife kommt dann von selbst nicht mehr
    // dran, und das ist die Absicht.
    ota.poll(network.connected(), app.secrets().get(SecretKey::OtaPass));

    web.poll();
    bootGuard.poll(nowMs);
    app.tick();

    // Erst antworten, dann handeln -- sonst saehe der Browser keine
    // Bestaetigung.
    app.refreshWebStatus();
    app.applyWebAction(web.takeAction());

    if (nowMs - lastStatusMs >= kStatusIntervalMs) {
        lastStatusMs = nowMs;
        static const char* kStateName[] = {"tag", "nacht", "aus"};
        const App::Snapshot& s = app.snapshot();
        Serial.printf("[status] %02u:%02u  %s  %s  wlan=%s  mqtt=%s  rssi=%d  heap=%u  %umA\n",
                      s.hours, s.minutes, kStateName[uint8_t(s.state)], faultName(s.health.fault),
                      network.connected() ? "ja" : "nein",
                      app.mqtt().connected() ? "ja" : "nein", network.rssi(), ESP.getFreeHeap(),
                      unsigned(s.currentMa));
    }
}
