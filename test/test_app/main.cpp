// Host-Tests fuer die Hauptschleife.
//
// Sie war bis zum Ports-Umbau die einzige ungetestete Stelle im Projekt --
// ausgerechnet die, die entscheidet, wann welcher Zustand gilt.
//
//   pio test -e native

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "wordclock/App.h"

using namespace wordclock;

void setUp(void) {}
void tearDown(void) {}

static char g_msg[256];
static const char* msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(g_msg, sizeof(g_msg), fmt, a);
    va_end(a);
    return g_msg;
}

// --- Attrappen -------------------------------------------------------------

struct FakeStrip : IStrip {
    Frame last;
    uint32_t shows = 0;
    void show(const Frame& f) override {
        last = f;
        ++shows;
    }
};

struct FakeClock : IClock {
    uint32_t ms = 0;
    uint8_t h = 12, m = 0;
    bool synced = true;
    uint32_t syncAge = 60;

    uint32_t nowMs() override { return ms; }
    bool everSynced() const override { return synced; }
    uint32_t secondsSinceSync() const override { return syncAge; }
    void localHm(uint8_t& hh, uint8_t& mm) const override {
        hh = h;
        mm = m;
    }
    void setTime(uint8_t hh, uint8_t mm) {
        h = hh;
        m = mm;
    }
};

struct FakeNetwork : INetwork {
    bool up = true, ap = false;
    std::string ssid, pass;
    uint32_t applyCalls = 0;

    bool connected() const override { return up; }
    bool apActive() const override { return ap; }
    int rssi() const override { return -60; }
    const char* ip() const override { return "192.168.1.42"; }
    void applyCredentials(const char* s, const char* p) override {
        ssid = s ? s : "";
        pass = p ? p : "";
        ++applyCalls;
    }
};

struct FakeStorage : IStorage {
    Config stored;
    Secrets storedSecrets;
    bool healthy = true;
    bool hasFile = false;
    bool hasSecrets = false;
    uint32_t saves = 0;
    uint32_t secretSaves = 0;

    bool ok() const override { return healthy; }
    bool load(Config& cfg) override {
        if (!hasFile) return false;
        cfg = stored;
        cfg.clearDirty();
        return true;
    }
    bool save(Config& cfg) override {
        stored = cfg;
        cfg.clearDirty();
        ++saves;
        return true;
    }
    bool loadSecrets(Secrets& sec) override {
        if (!hasSecrets) return false;
        sec = storedSecrets;
        sec.clearDirty();
        return true;
    }
    bool saveSecrets(Secrets& sec) override {
        storedSecrets = sec;
        sec.clearDirty();
        ++secretSaves;
        return true;
    }
};

struct FakeSystem : ISystemInfo {
    std::vector<std::string> lines;
    uint32_t restarts = 0;
    uint32_t freeHeap() const override { return 40000; }
    void log(const char* l) override { lines.emplace_back(l); }
    void restart() override { ++restarts; }
};

struct Rig {
    FakeStrip strip;
    FakeClock clock;
    FakeNetwork network;
    FakeStorage storage;
    FakeSystem system;
    App app;

    Rig() : app(makePorts()) {}

    Ports makePorts() {
        Ports p;
        p.strip = &strip;
        p.clock = &clock;
        p.network = &network;
        p.storage = &storage;
        p.mqtt = nullptr;  // ohne Broker
        p.system = &system;
        return p;
    }

    // Laesst die Schleife laufen, als waere die Zeit vergangen.
    void run(uint32_t durationMs, uint32_t stepMs = 50) {
        for (uint32_t t = 0; t < durationMs; t += stepMs) {
            clock.ms += stepMs;
            app.tick();
        }
    }

    uint16_t litLetters() const {
        uint16_t n = 0;
        for (uint16_t c = 0; c < kLetterCount; ++c)
            if (strip.last.cell(c) != kBlack) ++n;
        return n;
    }
    uint8_t litDots() const {
        uint8_t n = 0;
        for (uint8_t d = 0; d < kDotCount; ++d)
            if (strip.last.dot(d) != kBlack) ++n;
        return n;
    }
};

// --- Grundlauf -------------------------------------------------------------

static void test_draws_the_time() {
    Rig r;
    r.app.begin();
    r.clock.setTime(7, 45);
    r.run(2000);

    TEST_ASSERT_TRUE_MESSAGE(r.strip.shows > 10, "es muss regelmaessig gezeichnet werden");
    // ES(2) + IST(3) + DREIVIERTEL(11) + ACHT(4)
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(20, r.litLetters(),
                                     "ES IST DREIVIERTEL ACHT sind 20 Buchstaben");
}

static void test_frame_interval_is_respected() {
    Rig r;
    r.app.begin();
    r.app.setFrameInterval(100);
    r.run(1000, 10);  // 100 Ticks, aber nur alle 100 ms ein Bild
    TEST_ASSERT_TRUE_MESSAGE(r.strip.shows <= 12, msg("%u Bilder statt ~10", r.strip.shows));
}

// --- Darstellungszustaende --------------------------------------------------

static void test_day_night_off_over_a_day() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::NightEnabled, 1);
    r.app.config().set(ConfigKey::NightFrom, 22 * 60);
    r.app.config().set(ConfigKey::NightTo, 7 * 60);
    r.app.config().set(ConfigKey::OffEnabled, 1);
    r.app.config().set(ConfigKey::OffFrom, 23 * 60);
    r.app.config().set(ConfigKey::OffTo, 6 * 60);

    struct Case {
        uint8_t h, m;
        DisplayState expect;
        const char* what;
    };
    const Case cases[] = {
        {12, 0, DisplayState::Day, "Mittag"},
        {21, 59, DisplayState::Day, "kurz vor Nacht"},
        {22, 0, DisplayState::Night, "Nachtbeginn"},
        {22, 59, DisplayState::Night, "spaeter Abend"},
        {23, 0, DisplayState::Off, "Aus-Beginn"},
        {3, 0, DisplayState::Off, "mitten in der Nacht"},
        {6, 0, DisplayState::Night, "Aus-Ende, noch Nacht"},
        {7, 0, DisplayState::Day, "Morgen"},
    };

    for (const Case& c : cases) {
        r.clock.setTime(c.h, c.m);
        r.run(300);
        TEST_ASSERT_TRUE_MESSAGE(r.app.snapshot().state == c.expect, c.what);
    }
}

static void test_off_state_darkens_the_word_field() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::OffEnabled, 1);
    r.app.config().set(ConfigKey::OffFrom, 23 * 60);
    r.app.config().set(ConfigKey::OffTo, 6 * 60);

    r.clock.setTime(12, 0);
    r.run(2000);
    TEST_ASSERT_TRUE_MESSAGE(r.litLetters() > 0, "tagsueber leuchtet das Wortfeld");

    r.clock.setTime(23, 30);
    r.run(4000);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, r.litLetters(), "im Aus-Zustand bleibt alles dunkel");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r.litDots(), "auch die Eckpunkte, solange nichts kaputt ist");
}

// Der Gast schlaeft -- aber ein kritischer Fehler bricht durch (DESIGN 6).
static void test_critical_fault_breaks_through_off_state() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::OffEnabled, 1);
    r.app.config().set(ConfigKey::OffFrom, 23 * 60);
    r.app.config().set(ConfigKey::OffTo, 6 * 60);
    r.clock.setTime(23, 30);
    r.network.up = false;  // kein WLAN -> kritisch, Code 1

    bool sawDot = false;
    for (int i = 0; i < 200 && !sawDot; ++i) {
        r.run(50);
        if (r.litDots() > 0) sawDot = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDot, "kritischer Fehler muss den Aus-Zustand durchbrechen");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, r.litLetters(), "das Wortfeld bleibt trotzdem dunkel");
}

// Ohne je gestellte Zeit gibt es keine Zeitfenster -- sonst koennte die Uhr im
// Aus-Zustand haengen bleiben und nie wieder erscheinen.
static void test_never_synced_shows_nothing_but_does_not_lock_into_off() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::OffEnabled, 1);
    r.app.config().set(ConfigKey::OffFrom, 0);
    r.app.config().set(ConfigKey::OffTo, 23 * 60 + 59);
    r.clock.synced = false;

    r.run(2000);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, r.litLetters(), "ohne Zeit wird nichts angezeigt");
    TEST_ASSERT_TRUE_MESSAGE(r.app.snapshot().state == DisplayState::Day,
                             "ohne Zeit gilt Tag, nicht Aus");

    bool sawDot = false;
    for (int i = 0; i < 100 && !sawDot; ++i) {
        r.run(50);
        if (r.litDots() > 0) sawDot = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDot, "der Fehlercode muss sichtbar sein");
}

static void test_stale_time_keeps_running() {
    Rig r;
    r.app.begin();
    r.clock.syncAge = kSyncWarnSeconds + 3600;  // Warnung, aber nicht kritisch
    r.clock.setTime(7, 45);
    r.run(2000);

    TEST_ASSERT_TRUE_MESSAGE(r.litLetters() > 0, "veraltete Zeit wird weiter angezeigt");
    TEST_ASSERT_TRUE(r.app.snapshot().health.severity == Severity::Warning);
}

// --- Konfiguration ---------------------------------------------------------

static void test_config_change_takes_effect_without_restart() {
    Rig r;
    r.app.begin();
    r.clock.setTime(7, 45);
    r.app.config().set(ConfigKey::Brightness, 255);
    r.run(3000);
    const uint32_t bright = r.app.snapshot().currentMa;

    r.app.config().set(ConfigKey::Brightness, 20);
    r.run(3000);
    const uint32_t dim = r.app.snapshot().currentMa;

    TEST_ASSERT_TRUE_MESSAGE(dim < bright / 2,
                             msg("Helligkeit wirkt nicht: %u -> %u mA", bright, dim));
}

static void test_loads_stored_config_at_start() {
    Rig r;
    r.storage.hasFile = true;
    r.storage.stored.set(ConfigKey::Brightness, 111);
    r.app.begin();
    TEST_ASSERT_EQUAL_INT32(111, r.app.config().get(ConfigKey::Brightness));
}

// Gewartet wird auf Ruhe, nicht auf die erste Aenderung -- sonst schriebe ein
// gezogener Schieberegler mitten im Ziehen in den Flash.
static void test_autosave_waits_for_quiet() {
    Rig r;
    r.app.begin();

    // Waehrend des "Ziehens" alle 500 ms ein neuer Wert.
    for (int i = 0; i < 10; ++i) {
        r.app.config().set(ConfigKey::Brightness, int32_t(100 + i));
        r.run(500);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.storage.saves, "waehrend des Ziehens darf nichts fallen");

    r.run(kAutosaveQuietMs + 500);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, r.storage.saves, "nach der Ruhephase genau einmal");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(109, r.storage.stored.get(ConfigKey::Brightness),
                                    "gespeichert wird der letzte Wert");

    r.run(10000);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, r.storage.saves, "ohne Aenderung nicht erneut");
}

static void test_broken_storage_does_not_stop_the_clock() {
    Rig r;
    r.storage.healthy = false;
    r.app.begin();
    r.clock.setTime(7, 45);
    r.run(2000);

    TEST_ASSERT_TRUE_MESSAGE(r.app.snapshot().health.fault == Fault::ConfigBroken,
                             "Dateisystemfehler muss gemeldet werden");
    TEST_ASSERT_TRUE_MESSAGE(r.strip.shows > 10, "die Uhr laeuft trotzdem weiter");
}

// --- Benachrichtigungen ----------------------------------------------------

static void test_notification_tints_and_expires() {
    Rig r;
    r.app.begin();
    r.clock.setTime(7, 45);
    // Bewusst NICHT weiss: dort waeren Rot- und Blauanteil gleich und die
    // Rueckkehr zur Uhrenfarbe nicht nachweisbar.
    r.app.config().set(ConfigKey::Color, 0xFFB43C);
    r.run(3000);

    NotifyRequest req;
    req.id = "fenster";
    req.prio = 50;
    req.style = NotifyStyle::Tint;
    req.color = {0, 0, 255};
    req.ttlSeconds = 5;
    TEST_ASSERT_TRUE(r.app.notifications().push(req, r.clock.ms));

    r.run(3000);
    const Rgb tinted = r.strip.last.cell(span(Word::Es).cell);
    TEST_ASSERT_TRUE_MESSAGE(tinted.b > tinted.r, "Benachrichtigung muss das Wortfeld umfaerben");

    r.run(8000);  // TTL abgelaufen
    const Rgb back = r.strip.last.cell(span(Word::Es).cell);
    TEST_ASSERT_TRUE_MESSAGE(back.r > back.b, "nach Ablauf muss die Uhrenfarbe zurueckkehren");
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_draws_the_time);
    RUN_TEST(test_frame_interval_is_respected);
    RUN_TEST(test_day_night_off_over_a_day);
    RUN_TEST(test_off_state_darkens_the_word_field);
    RUN_TEST(test_critical_fault_breaks_through_off_state);
    RUN_TEST(test_never_synced_shows_nothing_but_does_not_lock_into_off);
    RUN_TEST(test_stale_time_keeps_running);
    RUN_TEST(test_config_change_takes_effect_without_restart);
    RUN_TEST(test_loads_stored_config_at_start);
    RUN_TEST(test_autosave_waits_for_quiet);
    RUN_TEST(test_broken_storage_does_not_stop_the_clock);
    RUN_TEST(test_notification_tints_and_expires);
    return UNITY_END();
}
