// Wortuhr — Zusammenbau. Siehe DESIGN.md.
//
// Erste Inbetriebnahme: WLAN, NTP, Uhrzeit auf dem Panel, Gesundheitskanal auf
// den Eckpunkten. Konfiguration, MQTT und Webapp folgen.

#include <Arduino.h>

#if !__has_include("secrets.h")
#error "include/secrets_example.h nach include/secrets.h kopieren und ausfuellen."
#endif
#include "secrets.h"

#include "StripAdapter.h"
#include "TimeSource.h"
#include "WifiConnector.h"
#include "wordclock/ClockRenderer.h"
#include "wordclock/Compositor.h"
#include "wordclock/DotRenderer.h"
#include "wordclock/Health.h"
#include "wordclock/Notify.h"

using namespace wordclock;

namespace {

constexpr uint8_t kDataPin = D1;  // GPIO5
constexpr uint16_t kFrameIntervalMs = 50;
constexpr uint16_t kStatusIntervalMs = 5000;

// Vorlaeufig fest verdrahtet. Kommt spaeter aus dem Konfigurations-Schema.
constexpr uint8_t kBrightness = 90;

StripAdapter strip(kDataPin);
WifiConnector wifi;
TimeSource clockTime;

ClockRenderer clockRenderer;
DotRenderer dotRenderer;
Compositor compositor;
NotifyStack notifications;
Overlay overlay;

uint32_t lastFrameMs = 0;
uint32_t lastStatusMs = 0;

void configureStyles() {
    ClockStyle cs;
    cs.color = {255, 180, 60};
    cs.transition = Transition::Staggered;
    cs.transitionMs = 400;
    clockRenderer.setStyle(cs);

    DotStyle ds;
    ds.clockColor = cs.color;
    dotRenderer.setStyle(ds);

    compositor.setSmoothing(128);
    compositor.setCurrentLimit(kDefaultCurrentLimitMa);
}

HealthInputs gatherHealth(uint32_t nowMs) {
    HealthInputs in;
    in.configOk = true;   // noch kein LittleFS
    in.apActive = false;  // AP kommt mit der Webapp
    in.mqttEnabled = false;
    in.mqttConnected = false;
    in.wifiConnected = wifi.connected();
    in.everSynced = clockTime.everSynced();
    in.secondsSinceSync = clockTime.secondsSinceSync(nowMs);
    return in;
}

#ifdef DOT_MAPPING_TEST
// Beantwortet die beiden Fragen, die sich nur am Geraet klaeren lassen:
// Stimmt die angenommene Ausrichtung des Rasters, und welcher Strip-Index
// sitzt in welcher Ecke?
void runMappingTest() {
    Serial.println("\n=== Mapping-Test ===");

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
        strip.showSingle(dotIndex(d), {120, 0, 0});
        Serial.printf("   Punkt %u  = Strip-Index %u  -> welche Ecke?\n", d, dotIndex(d));
        delay(3000);
    }

    Frame off;
    off.clear();
    strip.show(off);
    Serial.println("=== fertig ===\n");
}
#endif

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\n\nWortuhr — Panel %ux%u, %u LEDs, Pin D1\n", kWidth, kHeight, kLedCount);
    Serial.printf("Build: %s\n", __TIMESTAMP__);

    strip.begin();
    configureStyles();

#ifdef DOT_MAPPING_TEST
    runMappingTest();
#endif

    // Ab hier wird NICHTS mehr blockiert: loop() wird immer erreicht, auch
    // ohne WLAN. Die Uhr bleibt unter allen Umstaenden eine Uhr (DESIGN 9.1).
    wifi.begin(WIFI_SSID, WIFI_PASS);
    clockTime.begin();

    Serial.println("[setup] fertig, loop laeuft");
}

void loop() {
    const uint32_t nowMs = millis();

    wifi.tick(nowMs);
    clockTime.tick(nowMs);
    notifications.tick(nowMs, /*mqttConnected=*/false);

    if (nowMs - lastFrameMs < kFrameIntervalMs) return;
    lastFrameMs = nowMs;

    const HealthState health = evaluate(gatherHealth(nowMs));

    uint8_t hours = 0, minutes = 0;
    if (!health.wordFieldDark) clockTime.localHm(hours, minutes);

    Frame base;
    base.clear();

    // Ohne je gestellte Zeit bleibt das Wortfeld dunkel -- lieber nichts als
    // etwas Erfundenes (DESIGN 5).
    if (!health.wordFieldDark) clockRenderer.render(base, hours, minutes, nowMs);

    dotRenderer.render(base, health, minutes, nowMs, /*panelOff=*/false);

    Modifiers mod;
    mod.brightness = kBrightness;
    overlay.clear();
    notifications.apply(mod, overlay, nowMs,
                        clockRenderer.style().breathDepth > 0);

    strip.show(compositor.step(base, overlay, mod));

    if (nowMs - lastStatusMs >= kStatusIntervalMs) {
        lastStatusMs = nowMs;
        Serial.printf("[status] %02u:%02u  %s  wlan=%s  rssi=%d  heap=%u  %umA\n", hours,
                      minutes, faultName(health.fault), wifi.connected() ? "ja" : "nein",
                      WiFi.RSSI(), ESP.getFreeHeap(),
                      unsigned(estimateCurrentMa(compositor.current())));
    }
}
