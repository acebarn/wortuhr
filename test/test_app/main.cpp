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
#include <set>
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

// Ein Telegramm mit gleicher id ueberschreibt denselben Platz im Stapel. Ein
// Zeigervergleich haette den Wechsel deshalb nie bemerkt -- alle Animationen
// saehen aus wie die erste.
static void test_switching_animation_on_same_channel() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::NightEnabled, 0);
    r.app.config().set(ConfigKey::OffEnabled, 0);
    r.clock.setTime(12, 0);

    auto play = [&](const char* name) {
        NotifyRequest req;
        req.id = "gal";           // bewusst immer derselbe Kanal
        req.prio = 90;
        req.style = NotifyStyle::Anim;
        req.anim = name;
        req.ttlSeconds = 60;
        r.app.notifications().push(req, r.clock.ms);
        r.run(1500);
        std::string sig;
        for (uint16_t c = 0; c < kLetterCount; c += 5) {
            const Rgb v = r.strip.last.cell(c);
            sig += char('0' + v.r / 32);
            sig += char('0' + v.g / 32);
            sig += char('0' + v.b / 32);
        }
        return sig;
    };

    const std::string matrix = play("matrix");
    const std::string feuer = play("feuer");
    const std::string plasma = play("plasma");

    TEST_ASSERT_TRUE_MESSAGE(matrix != feuer, "Wechsel auf feuer wurde nicht uebernommen");
    TEST_ASSERT_TRUE_MESSAGE(feuer != plasma, "Wechsel auf plasma wurde nicht uebernommen");
}


// --- Ambient-Modus ----------------------------------------------------------
//
// Spielt die Presets in zufaelliger Reihenfolge, jedes eine Minute lang.

// Signatur der laufenden Animation. Zwei Presets koennen dieselbe Art haben --
// erst mit der Palette und dem Tempo sind sie auseinanderzuhalten.
static std::string animSignature(const App& app) {
    const AnimParams& p = app.animator().params();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s/%u,%u,%u/%u,%u,%u/%u", animName(app.animator().kind()),
                  p.from.r, p.from.g, p.from.b, p.to.r, p.to.g, p.to.b, p.speed);
    return buf;
}

static void test_ambient_is_off_by_default() {
    Rig r;
    r.app.begin();
    r.run(3000);
    TEST_ASSERT_FALSE_MESSAGE(r.app.animator().running(),
                              "ohne Schalter darf nichts ueber der Uhrzeit liegen");
}

static void test_ambient_starts_and_switches_every_minute() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::Ambient, 1);

    r.run(1000);
    TEST_ASSERT_TRUE_MESSAGE(r.app.animator().running(), "Ambient laeuft nicht an");
    const std::string first = animSignature(r.app);

    // Kurz vor der Minute muss noch dieselbe laufen -- sonst waere es Zufall,
    // dass ueberhaupt gewechselt wird.
    r.run(50000);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(first.c_str(), animSignature(r.app).c_str(),
                                     "nach 51 s haette noch nicht gewechselt werden duerfen");

    r.run(12000);
    TEST_ASSERT_TRUE_MESSAGE(first != animSignature(r.app),
                             "nach gut einer Minute muss etwas anderes laufen");
}

// Zufaellige Reihenfolge heisst hier: jedes Preset einmal, dann erst wieder von
// vorn. Gezogene Lose duerften dasselbe zweimal hintereinander bringen, und das
// faellt genau dann auf, wenn jemand hinsieht.
static void test_ambient_plays_every_preset_before_repeating() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::Ambient, 1);
    r.run(1000);

    std::set<std::string> seen;
    std::string prev;
    for (uint8_t i = 0; i < kAnimPresetCount; ++i) {
        const std::string sig = animSignature(r.app);
        TEST_ASSERT_TRUE_MESSAGE(seen.insert(sig).second,
                                 msg("%s kam zweimal in derselben Runde", sig.c_str()));
        prev = sig;
        r.run(kAmbientSwitchMs + 500);
        TEST_ASSERT_TRUE_MESSAGE(prev != animSignature(r.app),
                                 "zwei gleiche direkt hintereinander");
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kAnimPresetCount, uint8_t(seen.size()),
                                    "eine Runde muss jedes Preset genau einmal zeigen");
}

// HomeAssistant und Stundenschlag sind Absicht, Ambient ist Tapete.
static void test_ambient_yields_to_homeassistant() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::Ambient, 1);
    r.run(1000);
    TEST_ASSERT_TRUE(r.app.animator().running());

    NotifyRequest req;
    req.id = "klingel";
    req.prio = 90;
    req.style = NotifyStyle::Anim;
    req.anim = "matrix";
    req.ttlSeconds = 10;
    r.app.notifications().push(req, r.clock.ms);

    r.run(500);
    AnimKind k;
    AnimParams p;
    resolveAnim("matrix", k, p);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fall", animName(r.app.animator().kind()),
                                     "die HA-Animation muss Ambient verdraengen");
    TEST_ASSERT_EQUAL_UINT8(p.from.g, r.app.animator().params().from.g);

    // Nach Ablauf uebernimmt Ambient wieder.
    r.run(12000);
    TEST_ASSERT_TRUE_MESSAGE(r.app.animator().running(), "Ambient muss danach weiterlaufen");
}

// Nachts und im Aus-Zustand ruht die Flaeche. Ein Ambient-Modus, der um drei
// Uhr morgens Konfetti wirft, waere genau der Grund, warum jemand den Stecker
// zieht (DESIGN 7.2).
static void test_ambient_is_silent_at_night() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::Ambient, 1);
    r.app.config().set(ConfigKey::NightEnabled, 1);
    r.run(1000);
    TEST_ASSERT_TRUE(r.app.animator().running());

    r.clock.setTime(23, 30);
    r.run(1000);
    TEST_ASSERT_FALSE_MESSAGE(r.app.animator().running(), "nachts muss Ambient schweigen");

    r.clock.setTime(12, 0);
    r.run(1000);
    TEST_ASSERT_TRUE_MESSAGE(r.app.animator().running(), "tagsueber laeuft es wieder");
}

static void test_ambient_stops_when_switched_off() {
    Rig r;
    r.app.begin();
    r.app.config().set(ConfigKey::Ambient, 1);
    r.run(1000);
    TEST_ASSERT_TRUE(r.app.animator().running());

    r.app.config().set(ConfigKey::Ambient, 0);
    r.run(500);
    TEST_ASSERT_FALSE_MESSAGE(r.app.animator().running(), "Ausschalten muss sofort wirken");
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
    RUN_TEST(test_switching_animation_on_same_channel);
    RUN_TEST(test_ambient_is_off_by_default);
    RUN_TEST(test_ambient_starts_and_switches_every_minute);
    RUN_TEST(test_ambient_plays_every_preset_before_repeating);
    RUN_TEST(test_ambient_yields_to_homeassistant);
    RUN_TEST(test_ambient_is_silent_at_night);
    RUN_TEST(test_ambient_stops_when_switched_off);
    return UNITY_END();
}
