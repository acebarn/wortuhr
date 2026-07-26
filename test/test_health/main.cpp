// Host-Tests fuer Gesundheitsbewertung und Eckpunkt-Darstellung.
//
//   pio test -e native

#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <initializer_list>

#include "wordclock/DotRenderer.h"
#include "wordclock/Health.h"

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

// Ein rundum gesundes Geraet als Ausgangspunkt.
static HealthInputs healthy() {
    HealthInputs in;
    in.configOk = true;
    in.wifiConnected = true;
    in.apActive = false;
    in.mqttConnected = true;
    in.everSynced = true;
    in.secondsSinceSync = 60;
    return in;
}

// --- Bewertung -------------------------------------------------------------

static void test_healthy_is_silent() {
    const HealthState s = evaluate(healthy());
    TEST_ASSERT_TRUE(s.severity == Severity::Ok);
    TEST_ASSERT_TRUE(s.fault == Fault::None);
    TEST_ASSERT_TRUE_MESSAGE(s.rhythm == Rhythm::Steady, "gesund muss ruhig sein");
    TEST_ASSERT_EQUAL_UINT8(0, s.code);
    TEST_ASSERT_FALSE(s.wordFieldDark);
}

static void test_fault_codes_match_the_design() {
    struct Case {
        const char* what;
        HealthInputs in;
        Fault fault;
        uint8_t code;
        Rhythm rhythm;
    };

    HealthInputs noWifi = healthy();
    noWifi.wifiConnected = false;

    HealthInputs noTime = healthy();
    noTime.everSynced = false;

    HealthInputs broken = healthy();
    broken.configOk = false;

    HealthInputs ap = healthy();
    ap.wifiConnected = false;
    ap.apActive = true;

    const Case cases[] = {
        {"kein WLAN", noWifi, Fault::NoWifi, 1, Rhythm::Blinking},
        {"keine Zeit", noTime, Fault::NoTime, 2, Rhythm::Blinking},
        {"Config kaputt", broken, Fault::ConfigBroken, 3, Rhythm::Blinking},
        {"AP-Modus", ap, Fault::ApMode, 4, Rhythm::Chase},
    };

    for (const Case& c : cases) {
        const HealthState s = evaluate(c.in);
        TEST_ASSERT_TRUE_MESSAGE(s.severity == Severity::Critical,
                                 msg("%s muss kritisch sein", c.what));
        TEST_ASSERT_TRUE_MESSAGE(s.fault == c.fault, msg("%s: falsche Stoerung", c.what));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.code, s.code, msg("%s: falscher Code", c.what));
        TEST_ASSERT_TRUE_MESSAGE(s.rhythm == c.rhythm, msg("%s: falscher Rhythmus", c.what));
    }
}

static void test_mqtt_down_is_only_a_warning() {
    HealthInputs in = healthy();
    in.mqttConnected = false;
    const HealthState s = evaluate(in);

    TEST_ASSERT_TRUE_MESSAGE(s.severity == Severity::Warning,
                             "HomeAssistant weg darf die Uhr nicht kritisch machen");
    TEST_ASSERT_TRUE(s.fault == Fault::MqttDown);
    TEST_ASSERT_TRUE(s.rhythm == Rhythm::Breathing);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, s.code, "Warnung darf die Minutenanzeige nicht belegen");
    TEST_ASSERT_FALSE_MESSAGE(s.wordFieldDark, "die Uhrzeit stimmt weiterhin");
}

// Die Schwellen sind aus dem Anzeigeschritt hergeleitet, nicht gefuehlt --
// deshalb werden die Grenzen genau geprueft.
static void test_time_trust_thresholds() {
    struct Case {
        uint32_t age;
        Severity sev;
        Fault fault;
    };

    const Case cases[] = {
        {0, Severity::Ok, Fault::None},
        {kSyncWarnSeconds - 1, Severity::Ok, Fault::None},
        {kSyncWarnSeconds, Severity::Warning, Fault::TimeStale},
        {kSyncCriticalSeconds - 1, Severity::Warning, Fault::TimeStale},
        {kSyncCriticalSeconds, Severity::Critical, Fault::NoTime},
        {kSyncCriticalSeconds * 10, Severity::Critical, Fault::NoTime},
    };

    for (const Case& c : cases) {
        HealthInputs in = healthy();
        in.secondsSinceSync = c.age;
        const HealthState s = evaluate(in);
        TEST_ASSERT_TRUE_MESSAGE(s.severity == c.sev,
                                 msg("Alter %us: falscher Schweregrad", unsigned(c.age)));
        TEST_ASSERT_TRUE_MESSAGE(s.fault == c.fault,
                                 msg("Alter %us: falsche Stoerung", unsigned(c.age)));
    }
}

// Eine veraltete Zeit wird weiter angezeigt -- bei 5 Minuten Anzeigeschritt ist
// sie tagelang korrekt. Eine nie gestellte Uhr waere reine Erfindung.
static void test_word_field_darkens_only_when_never_synced() {
    HealthInputs never = healthy();
    never.everSynced = false;
    TEST_ASSERT_TRUE_MESSAGE(evaluate(never).wordFieldDark,
                             "ohne je gestellte Zeit darf nichts angezeigt werden");

    HealthInputs veryStale = healthy();
    veryStale.secondsSinceSync = kSyncCriticalSeconds * 5;
    TEST_ASSERT_FALSE_MESSAGE(evaluate(veryStale).wordFieldDark,
                              "veraltete Zeit wird weiter angezeigt");

    HealthInputs offline = healthy();
    offline.wifiConnected = false;
    TEST_ASSERT_FALSE_MESSAGE(evaluate(offline).wordFieldDark,
                              "ohne WLAN laeuft die Uhr weiter");
}

static void test_priority_order() {
    // Alles gleichzeitig kaputt -> das Grundlegendste gewinnt.
    HealthInputs all;
    all.configOk = false;
    all.wifiConnected = false;
    all.apActive = true;
    all.mqttConnected = false;
    all.everSynced = false;
    TEST_ASSERT_TRUE_MESSAGE(evaluate(all).fault == Fault::ConfigBroken,
                             "defekte Konfiguration macht alles andere unbrauchbar");

    // AP-Modus schlaegt "kein WLAN": er ist die Folge davon und der einzige
    // Zustand, in dem der Benutzer etwas tun kann.
    HealthInputs ap = all;
    ap.configOk = true;
    TEST_ASSERT_TRUE_MESSAGE(evaluate(ap).fault == Fault::ApMode,
                             "AP-Modus muss vor 'kein WLAN' stehen");

    // Ursache vor Folge: ohne WLAN gibt es auch kein NTP.
    HealthInputs wifi = ap;
    wifi.apActive = false;
    TEST_ASSERT_TRUE_MESSAGE(evaluate(wifi).fault == Fault::NoWifi,
                             "kein WLAN muss vor 'keine Zeit' stehen");

    // Zeit schlaegt MQTT.
    HealthInputs time = wifi;
    time.wifiConnected = true;
    TEST_ASSERT_TRUE_MESSAGE(evaluate(time).fault == Fault::NoTime,
                             "keine Zeit muss vor 'MQTT weg' stehen");
}

// --- Darstellung -----------------------------------------------------------

static void test_healthy_dots_show_the_minute_steadily() {
    DotRenderer r;
    const HealthState s = evaluate(healthy());

    for (uint8_t m = 0; m < 60; ++m) {
        Frame f;
        f.clear();
        r.render(f, s, m, 12345);

        uint8_t lit = 0;
        for (uint8_t d = 0; d < kDotCount; ++d)
            if (f.dot(d) != kBlack) ++lit;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(minuteDots(m), lit, msg("Minute %u", m));
    }

    // Ruhig heisst wirklich ruhig: ueber die Zeit darf sich nichts aendern.
    Frame a, b;
    a.clear();
    b.clear();
    r.render(a, s, 3, 0);
    r.render(b, s, 3, 987654);
    for (uint8_t d = 0; d < kDotCount; ++d)
        TEST_ASSERT_TRUE_MESSAGE(a.dot(d) == b.dot(d), "gesunde Punkte duerfen sich nicht bewegen");
}

// Der Renderer darf das Wortfeld unter keinen Umstaenden anfassen.
static void test_never_touches_the_letters() {
    DotRenderer r;
    HealthInputs in = healthy();
    in.mqttConnected = false;

    for (bool off : {false, true}) {
        Frame f;
        f.clear();
        f.fillLetters({50, 60, 70});
        for (uint32_t ms = 0; ms < 4000; ms += 41) {
            r.render(f, evaluate(in), 17, ms, off);
            for (uint16_t c = 0; c < kLetterCount; ++c)
                TEST_ASSERT_TRUE_MESSAGE(f.cell(c) == Rgb({50, 60, 70}),
                                         msg("Rasterzelle %u veraendert", c));
        }
    }
}

// Eine Warnung darf die Minutenanzeige nicht kosten -- ausser bei Minute 0,
// 5, 10 ... waere sie sonst unsichtbar.
static void test_warning_keeps_the_minute_but_stays_visible() {
    DotRenderer r;
    HealthInputs in = healthy();
    in.mqttConnected = false;
    const HealthState s = evaluate(in);

    for (uint8_t m = 0; m < 60; ++m) {
        const uint8_t expect = minuteDots(m) ? minuteDots(m) : uint8_t(1);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expect, r.dotCount(s, m), msg("Minute %u", m));
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, r.dotCount(s, 0),
                                    "bei Minute 0 muss die Warnung trotzdem sichtbar sein");
}

static void test_warning_breathes_but_never_goes_dark() {
    DotRenderer r;
    HealthInputs in = healthy();
    in.mqttConnected = false;
    const HealthState s = evaluate(in);

    uint8_t lo = 255, hi = 0;
    for (uint32_t ms = 0; ms < 9000; ms += 23) {
        Frame f;
        f.clear();
        r.render(f, s, 7, ms);
        const uint8_t v = f.dot(0).r;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    TEST_ASSERT_TRUE_MESSAGE(hi > lo, "Warnung muss sich bewegen");
    TEST_ASSERT_TRUE_MESSAGE(lo > 0, "Atmen darf nie ganz erloeschen, sonst ist es Blinken");
}

static void test_critical_blinks_fully_off() {
    DotRenderer r;
    HealthInputs in = healthy();
    in.wifiConnected = false;
    const HealthState s = evaluate(in);

    bool sawOn = false, sawOff = false;
    for (uint32_t ms = 0; ms < 3000; ms += 17) {
        Frame f;
        f.clear();
        r.render(f, s, 7, ms);
        if (f.dot(0) == kBlack) sawOff = true;
        else sawOn = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawOn && sawOff, "kritisch muss vollstaendig blinken");
}

static void test_critical_count_equals_the_code() {
    DotRenderer r;
    HealthInputs noWifi = healthy();
    noWifi.wifiConnected = false;
    HealthInputs broken = healthy();
    broken.configOk = false;

    // Zu einem Zeitpunkt, an dem das Blinken gerade an ist.
    for (const HealthInputs& in : {noWifi, broken}) {
        const HealthState s = evaluate(in);
        Frame f;
        f.clear();
        r.render(f, s, 33, 0);  // Minute 33 wuerde sonst 3 Punkte ergeben
        uint8_t lit = 0;
        for (uint8_t d = 0; d < kDotCount; ++d)
            if (f.dot(d) != kBlack) ++lit;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(s.code, lit, msg("Code %u falsch dargestellt", s.code));
    }
}

static void test_ap_mode_chases_all_four() {
    DotRenderer r;
    HealthInputs in = healthy();
    in.wifiConnected = false;
    in.apActive = true;
    const HealthState s = evaluate(in);

    TEST_ASSERT_EQUAL_UINT8(kDotCount, r.dotCount(s, 0));

    // Der helle Punkt muss wandern.
    int8_t firstBrightest = -1;
    bool moved = false;
    for (uint32_t ms = 0; ms < 2000; ms += 20) {
        Frame f;
        f.clear();
        r.render(f, s, 0, ms);

        int8_t brightest = 0;
        for (uint8_t d = 1; d < kDotCount; ++d)
            if (f.dot(d).b > f.dot(uint8_t(brightest)).b) brightest = int8_t(d);

        if (firstBrightest < 0) firstBrightest = brightest;
        else if (brightest != firstBrightest) moved = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(moved, "Lauflicht muss wandern");
}

// --- Aus-Zustand -----------------------------------------------------------

static void test_panel_off_silences_everything_but_critical() {
    DotRenderer r;

    // Gesund und Warnung schweigen.
    HealthInputs warn = healthy();
    warn.mqttConnected = false;
    for (const HealthInputs& in : {healthy(), warn}) {
        Frame f;
        f.clear();
        for (uint32_t ms = 0; ms < 4000; ms += 31) {
            r.render(f, evaluate(in), 33, ms, /*panelOff=*/true);
            for (uint8_t d = 0; d < kDotCount; ++d)
                TEST_ASSERT_TRUE_MESSAGE(f.dot(d) == kBlack,
                                         "im Aus-Zustand darf nur Kritisches leuchten");
        }
    }

    // Kritisches bricht durch -- und blinkt wie im Normalbetrieb.
    HealthInputs crit = healthy();
    crit.wifiConnected = false;
    bool sawLight = false;
    for (uint32_t ms = 0; ms < 3000; ms += 17) {
        Frame f;
        f.clear();
        r.render(f, evaluate(crit), 33, ms, /*panelOff=*/true);
        if (f.dot(0) != kBlack) sawLight = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawLight, "kritischer Fehler muss den Aus-Zustand durchbrechen");
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_is_silent);
    RUN_TEST(test_fault_codes_match_the_design);
    RUN_TEST(test_mqtt_down_is_only_a_warning);
    RUN_TEST(test_time_trust_thresholds);
    RUN_TEST(test_word_field_darkens_only_when_never_synced);
    RUN_TEST(test_priority_order);
    RUN_TEST(test_healthy_dots_show_the_minute_steadily);
    RUN_TEST(test_never_touches_the_letters);
    RUN_TEST(test_warning_keeps_the_minute_but_stays_visible);
    RUN_TEST(test_warning_breathes_but_never_goes_dark);
    RUN_TEST(test_critical_blinks_fully_off);
    RUN_TEST(test_critical_count_equals_the_code);
    RUN_TEST(test_ap_mode_chases_all_four);
    RUN_TEST(test_panel_off_silences_everything_but_critical);
    return UNITY_END();
}
