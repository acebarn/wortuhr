// Wortuhr — Zusammenbau. Siehe DESIGN.md.
//
// Erste Inbetriebnahme: WLAN, NTP, Uhrzeit auf dem Panel, Gesundheitskanal auf
// den Eckpunkten. Konfiguration, MQTT und Webapp folgen.

#include <Arduino.h>

#if !__has_include("secrets.h")
#error "include/secrets_example.h nach include/secrets.h kopieren und ausfuellen."
#endif
#include "secrets.h"

#include "ConfigStore.h"
#include "StripAdapter.h"
#include "TimeSource.h"
#include "WifiConnector.h"
#include "wordclock/ClockRenderer.h"
#include "wordclock/Compositor.h"
#include "wordclock/DotRenderer.h"
#include "wordclock/Health.h"
#include "wordclock/Notify.h"
#include "wordclock/Profiles.h"

using namespace wordclock;

namespace {

constexpr uint8_t kDataPin = D1;  // GPIO5
constexpr uint16_t kFrameIntervalMs = 50;
constexpr uint16_t kStatusIntervalMs = 5000;

StripAdapter strip(kDataPin);
ConfigStore store;
Config config;
WifiConnector wifi;
TimeSource clockTime;

ClockRenderer clockRenderer;
DotRenderer dotRenderer;
Compositor compositor;
NotifyStack notifications;
Overlay overlay;

uint32_t lastFrameMs = 0;
uint32_t lastStatusMs = 0;

// Aus der Konfiguration abgeleitet. Wird bei jedem Wechsel des
// Darstellungszustands neu gesetzt, nicht bei jedem Bild.
DisplayState appliedState = DisplayState::Day;
bool stylesApplied = false;

void applyStyles(DisplayState state) {
    clockRenderer.setStyle(clockStyleFor(config, state));
    dotRenderer.setStyle(dotStyleFor(config, state));
    compositor.setSmoothing(config.getU8(ConfigKey::Smoothing));
    compositor.setCurrentLimit(config.getU16(ConfigKey::CurrentLimit));
    appliedState = state;
    stylesApplied = true;
}

HealthInputs gatherHealth(uint32_t nowMs) {
    HealthInputs in;
    in.configOk = store.mounted();
    in.apActive = false;  // AP kommt mit der Webapp
    in.mqttEnabled = false;
    in.mqttConnected = false;
    in.wifiConnected = wifi.connected();
    in.everSynced = clockTime.everSynced();
    in.secondsSinceSync = clockTime.secondsSinceSync(nowMs);
    return in;
}

#ifdef CONFIG_SELFTEST
// Prueft den Persistenz-Rundlauf auf echtem Flash: schreiben, umbenennen,
// zurueckladen. Das Verhalten von LittleFS laesst sich auf dem Host nicht
// nachstellen, und ungeprueft wollen wir den Pfad nicht mitschleppen.
void runConfigSelfTest() {
    Serial.println("\n=== Config-Selbsttest ===");
    Serial.printf("  eingehaengt: %s\n", store.mounted() ? "ja" : "NEIN");

    constexpr int32_t kMarker = 137;
    config.set(ConfigKey::Brightness, kMarker);
    config.set(ConfigKey::NightFrom, 21 * 60 + 45);
    Serial.printf("  speichern... %s\n", store.save(config) ? "ok" : "FEHLGESCHLAGEN");

    Config reloaded;
    reloaded.set(ConfigKey::Brightness, 5);  // absichtlich verstellen
    Serial.printf("  laden...     %s\n", store.load(reloaded) ? "ok" : "FEHLGESCHLAGEN");

    const bool ok = reloaded.get(ConfigKey::Brightness) == kMarker &&
                    reloaded.get(ConfigKey::NightFrom) == 21 * 60 + 45;
    Serial.printf("  Rundlauf:    %s (Helligkeit %d, Nacht ab %d)\n", ok ? "BESTANDEN" : "FEHLER",
                  int(reloaded.get(ConfigKey::Brightness)), int(reloaded.get(ConfigKey::NightFrom)));

    // Sauberen Zustand hinterlassen: Vorgaben schreiben.
    config.reset();
    config.set(ConfigKey::Brightness, schemaOf(ConfigKey::Brightness).def);
    store.save(config);
    Serial.println("  Vorgaben zurueckgeschrieben");
    Serial.println("=== fertig ===\n");
}
#endif

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
        strip.showSingle(dotIndex(d), {120, 0, 0});
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

    // Ein unbenutzbares Dateisystem ist kein Grund, nicht zu laufen -- die Uhr
    // arbeitet dann mit Vorgaben weiter und meldet Code 3 auf den Eckpunkten.
    store.begin();
    store.load(config);
    applyStyles(DisplayState::Day);

#ifdef CONFIG_SELFTEST
    runConfigSelfTest();
#endif

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
    store.tickAutosave(config, nowMs);

    if (nowMs - lastFrameMs < kFrameIntervalMs) return;
    lastFrameMs = nowMs;

    const HealthState health = evaluate(gatherHealth(nowMs));

    uint8_t hours = 0, minutes = 0;
    if (!health.wordFieldDark) clockTime.localHm(hours, minutes);

    // Ohne gueltige Zeit gibt es keine Zeitfenster -- dann gilt Tag, sonst
    // koennte die Uhr im Aus-Zustand haengen bleiben und nie wieder erscheinen.
    const DisplayState state =
        health.wordFieldDark ? DisplayState::Day : displayStateFor(config, hours, minutes);
    const bool panelOff = (state == DisplayState::Off);

    if (!stylesApplied || state != appliedState || config.dirty()) applyStyles(state);

    Frame base;
    base.clear();

    // Ohne je gestellte Zeit bleibt das Wortfeld dunkel -- lieber nichts als
    // etwas Erfundenes (DESIGN 5).
    if (!health.wordFieldDark && !panelOff) clockRenderer.render(base, hours, minutes, nowMs);

    dotRenderer.render(base, health, minutes, nowMs, panelOff);

    Modifiers mod;
    mod.brightness = brightnessFor(config, state);
    overlay.clear();
    notifications.apply(mod, overlay, nowMs,
                        clockRenderer.style().breathDepth > 0);

    strip.show(compositor.step(base, overlay, mod));

    if (nowMs - lastStatusMs >= kStatusIntervalMs) {
        lastStatusMs = nowMs;
        static const char* kStateName[] = {"tag", "nacht", "aus"};
        Serial.printf("[status] %02u:%02u  %s  %s  wlan=%s  rssi=%d  heap=%u  %umA\n", hours,
                      minutes, kStateName[uint8_t(state)], faultName(health.fault),
                      wifi.connected() ? "ja" : "nein", WiFi.RSSI(), ESP.getFreeHeap(),
                      unsigned(estimateCurrentMa(compositor.current())));
    }
}
