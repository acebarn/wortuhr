// Host-Tests fuer Konfigurations-Schema, Serialisierung und Profile.
//
//   pio test -e native

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "wordclock/Animator.h"
#include "wordclock/ConfigJson.h"
#include "wordclock/Profiles.h"

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

// --- Schema-Konsistenz -----------------------------------------------------

// Die Tabelle wird von Hand gepflegt und muss zur Aufzaehlung passen. Ein
// verrutschter Eintrag waere sonst still: die Uhr faerbte sich nach der
// Nachthelligkeit oder aehnlich.
static void test_schema_is_consistent() {
    std::set<std::string> keys;
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const ConfigItem& it = kSchema[i];

        TEST_ASSERT_NOT_NULL_MESSAGE(it.key, msg("Eintrag %u ohne Schluessel", i));
        TEST_ASSERT_NOT_NULL_MESSAGE(it.label, msg("%s ohne Anzeigetext", it.key));
        TEST_ASSERT_NOT_NULL_MESSAGE(it.category, msg("%s ohne Kategorie", it.key));

        TEST_ASSERT_TRUE_MESSAGE(keys.insert(it.key).second,
                                 msg("Schluessel %s kommt doppelt vor", it.key));

        TEST_ASSERT_TRUE_MESSAGE(it.min <= it.max, msg("%s: min > max", it.key));
        TEST_ASSERT_TRUE_MESSAGE(it.def >= it.min && it.def <= it.max,
                                 msg("%s: Vorgabe liegt ausserhalb", it.key));

        if (it.type == ConfigType::Bool) {
            TEST_ASSERT_TRUE_MESSAGE(it.min == 0 && it.max == 1, msg("%s: Bool 0..1", it.key));
        }
        if (it.type == ConfigType::Choice) {
            TEST_ASSERT_NOT_NULL_MESSAGE(it.options, msg("%s: Choice ohne Optionen", it.key));
            TEST_ASSERT_EQUAL_INT32_MESSAGE(it.optionCount - 1, it.max,
                                            msg("%s: max passt nicht zur Optionszahl", it.key));
        }
        if (it.type == ConfigType::TimeOfDay) {
            TEST_ASSERT_TRUE_MESSAGE(it.min == 0 && it.max == 1439,
                                     msg("%s: Uhrzeit 0..1439", it.key));
        }
    }
}

// Der Stundenschlag waehlt aus einer Namensliste -- steht dort etwas, das der
// Animator nicht kennt, liefe zur vollen Stunde nichts, ohne Fehlermeldung.
static void test_chime_options_are_real_animations() {
    const ConfigItem& item = schemaOf(ConfigKey::ChimeStyle);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kAnimPresetCount, item.optionCount,
                                    "Auswahl und Presets muessen deckungsgleich sein");
    for (uint8_t i = 0; i < item.optionCount; ++i) {
        AnimKind k;
        AnimParams p;
        TEST_ASSERT_TRUE_MESSAGE(resolveAnim(item.options[i], k, p), item.options[i]);
    }
}

static void test_key_lookup_round_trips() {
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        ConfigKey k;
        TEST_ASSERT_TRUE_MESSAGE(Config::keyIndex(kSchema[i].key, k), kSchema[i].key);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(i, uint8_t(k), kSchema[i].key);
    }
    ConfigKey dummy;
    TEST_ASSERT_FALSE(Config::keyIndex("gibtsnicht", dummy));
    TEST_ASSERT_FALSE(Config::keyIndex(nullptr, dummy));
}

// --- Werte und Validierung -------------------------------------------------

static void test_defaults_are_applied() {
    Config cfg;
    for (uint8_t i = 0; i < kConfigCount; ++i)
        TEST_ASSERT_EQUAL_INT32_MESSAGE(kSchema[i].def, cfg.get(ConfigKey(i)), kSchema[i].key);
    TEST_ASSERT_FALSE_MESSAGE(cfg.dirty(), "frische Konfiguration ist nicht veraendert");
}

static void test_values_are_clamped_never_rejected() {
    Config cfg;
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const ConfigItem& it = kSchema[i];
        const ConfigKey k = ConfigKey(i);

        TEST_ASSERT_FALSE_MESSAGE(cfg.set(k, it.min - 1), msg("%s: zu klein muss melden", it.key));
        TEST_ASSERT_EQUAL_INT32_MESSAGE(it.min, cfg.get(k), msg("%s: nicht auf min geklemmt", it.key));

        TEST_ASSERT_FALSE_MESSAGE(cfg.set(k, it.max + 1), msg("%s: zu gross muss melden", it.key));
        TEST_ASSERT_EQUAL_INT32_MESSAGE(it.max, cfg.get(k), msg("%s: nicht auf max geklemmt", it.key));

        TEST_ASSERT_TRUE_MESSAGE(cfg.set(k, it.def), msg("%s: Vorgabe muss glatt durchgehen", it.key));
    }
}

static void test_dirty_only_on_real_change() {
    Config cfg;
    cfg.clearDirty();
    cfg.set(ConfigKey::Brightness, cfg.get(ConfigKey::Brightness));
    TEST_ASSERT_FALSE_MESSAGE(cfg.dirty(), "gleicher Wert ist keine Aenderung");
    cfg.set(ConfigKey::Brightness, 42);
    TEST_ASSERT_TRUE(cfg.dirty());
}

static void test_color_decoding() {
    Config cfg;
    cfg.set(ConfigKey::Color, 0x10FF80);
    const Rgb c = cfg.getColor(ConfigKey::Color);
    TEST_ASSERT_EQUAL_UINT8(0x10, c.r);
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.g);
    TEST_ASSERT_EQUAL_UINT8(0x80, c.b);
}

// --- Serialisierung --------------------------------------------------------

static void test_json_round_trip() {
    Config a;
    a.set(ConfigKey::Brightness, 123);
    a.set(ConfigKey::Color, 0x123456);
    a.set(ConfigKey::Gradient, 1);
    a.set(ConfigKey::Transition, 3);
    a.set(ConfigKey::NightFrom, 21 * 60 + 30);

    JsonDocument doc;
    toJson(a, doc.to<JsonObject>());

    std::string text;
    serializeJson(doc, text);

    JsonDocument back;
    TEST_ASSERT_FALSE(deserializeJson(back, text));

    Config b;
    const LoadReport rep = fromJson(b, back.as<JsonObjectConst>());

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kConfigCount, rep.applied, "alle Schluessel muessen ankommen");
    TEST_ASSERT_EQUAL_UINT8(0, rep.unknown);
    TEST_ASSERT_EQUAL_UINT8(0, rep.clamped);
    TEST_ASSERT_EQUAL_UINT8(0, rep.missing);

    for (uint8_t i = 0; i < kConfigCount; ++i)
        TEST_ASSERT_EQUAL_INT32_MESSAGE(a.get(ConfigKey(i)), b.get(ConfigKey(i)), kSchema[i].key);
}

// Eine Konfigurationsdatei aus einer aelteren Firmware darf nie dazu fuehren,
// dass die Uhr nicht mehr startet.
static void test_partial_and_unknown_keys_are_survivable() {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    o["brightness"] = 200;
    o["gibtsnicht"] = 5;         // Schluessel aus einer anderen Version
    o["ghost"] = 9999;           // weit ausserhalb
    o["color"] = "nicht_zahl";   // falscher Typ

    Config cfg;
    const LoadReport rep = fromJson(cfg, doc.as<JsonObjectConst>());

    TEST_ASSERT_EQUAL_INT32_MESSAGE(200, cfg.get(ConfigKey::Brightness), "gueltiger Wert kommt an");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(schemaOf(ConfigKey::Ghost).max, cfg.get(ConfigKey::Ghost),
                                    "zu grosser Wert wird geklemmt");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(schemaOf(ConfigKey::Color).def, cfg.get(ConfigKey::Color),
                                    "falscher Typ laesst die Vorgabe stehen");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, rep.unknown, "unbekannt und falscher Typ werden gezaehlt");
    TEST_ASSERT_EQUAL_UINT8(1, rep.clamped);
    TEST_ASSERT_TRUE_MESSAGE(rep.missing > 0, "fehlende Schluessel werden gemeldet");
}

static void test_schema_json_describes_every_item() {
    JsonDocument doc;
    schemaToJson(doc.to<JsonArray>());
    JsonArray arr = doc.as<JsonArray>();

    TEST_ASSERT_EQUAL_UINT8(kConfigCount, arr.size());

    // Nicht auf eine feste Zahl festnageln -- sonst bricht der Test bei jeder
    // neuen Auswahl-Einstellung, ohne dass etwas kaputt waere. Geprueft wird
    // die Regel: genau die Choice-Eintraege tragen Optionen.
    uint8_t i = 0;
    for (JsonObject o : arr) {
        TEST_ASSERT_TRUE(o["key"].is<const char*>());
        TEST_ASSERT_TRUE(o["type"].is<const char*>());
        TEST_ASSERT_TRUE(o["min"].is<int32_t>());
        TEST_ASSERT_TRUE(o["max"].is<int32_t>());
        TEST_ASSERT_TRUE(o["default"].is<int32_t>());

        const bool isChoice = kSchema[i].type == ConfigType::Choice;
        TEST_ASSERT_EQUAL_MESSAGE(isChoice, o["options"].is<JsonArray>(),
                                  msg("%s: Optionen passen nicht zum Typ", kSchema[i].key));
        if (isChoice)
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(kSchema[i].optionCount,
                                            o["options"].as<JsonArray>().size(), kSchema[i].key);
        ++i;
    }
}

// --- Zeitfenster -----------------------------------------------------------

// Der Umlauf ueber Mitternacht ist der Normalfall (22:00 bis 07:00) und die
// klassische Fehlerquelle.
static void test_window_wraps_over_midnight() {
    const uint16_t from = 22 * 60, to = 7 * 60;

    TEST_ASSERT_TRUE(inWindow(22 * 60, from, to));
    TEST_ASSERT_TRUE(inWindow(23 * 60 + 59, from, to));
    TEST_ASSERT_TRUE(inWindow(0, from, to));
    TEST_ASSERT_TRUE(inWindow(6 * 60 + 59, from, to));

    TEST_ASSERT_FALSE(inWindow(7 * 60, from, to));
    TEST_ASSERT_FALSE(inWindow(12 * 60, from, to));
    TEST_ASSERT_FALSE(inWindow(21 * 60 + 59, from, to));
}

static void test_window_without_wrap() {
    const uint16_t from = 9 * 60, to = 17 * 60;
    TEST_ASSERT_TRUE(inWindow(9 * 60, from, to));
    TEST_ASSERT_TRUE(inWindow(16 * 60, from, to));
    TEST_ASSERT_FALSE(inWindow(17 * 60, from, to));
    TEST_ASSERT_FALSE(inWindow(8 * 60, from, to));
    TEST_ASSERT_FALSE_MESSAGE(inWindow(500, 600, 600), "leeres Fenster gilt nie");
}

static void test_off_beats_night() {
    Config cfg;
    cfg.set(ConfigKey::NightEnabled, 1);
    cfg.set(ConfigKey::NightFrom, 22 * 60);
    cfg.set(ConfigKey::NightTo, 7 * 60);
    cfg.set(ConfigKey::OffEnabled, 1);
    cfg.set(ConfigKey::OffFrom, 23 * 60);
    cfg.set(ConfigKey::OffTo, 6 * 60);

    TEST_ASSERT_TRUE(displayStateFor(cfg, 12, 0) == DisplayState::Day);
    TEST_ASSERT_TRUE(displayStateFor(cfg, 22, 30) == DisplayState::Night);
    TEST_ASSERT_TRUE_MESSAGE(displayStateFor(cfg, 23, 30) == DisplayState::Off,
                             "Aus muss Nacht schlagen -- da schlaeft jemand");
    TEST_ASSERT_TRUE(displayStateFor(cfg, 3, 0) == DisplayState::Off);
    TEST_ASSERT_TRUE(displayStateFor(cfg, 6, 30) == DisplayState::Night);
    TEST_ASSERT_TRUE(displayStateFor(cfg, 7, 30) == DisplayState::Day);
}

static void test_disabled_windows_never_trigger() {
    Config cfg;
    cfg.set(ConfigKey::NightEnabled, 0);
    cfg.set(ConfigKey::OffEnabled, 0);
    for (uint8_t h = 0; h < 24; ++h)
        TEST_ASSERT_TRUE_MESSAGE(displayStateFor(cfg, h, 0) == DisplayState::Day,
                                 msg("Stunde %u", h));
}

// --- Profile ---------------------------------------------------------------

static void test_night_profile_is_quiet() {
    Config cfg;
    cfg.set(ConfigKey::Gradient, 1);
    cfg.set(ConfigKey::Ghost, 40);
    cfg.set(ConfigKey::BreathDepth, 30);
    cfg.set(ConfigKey::Transition, 3);

    const ClockStyle day = clockStyleFor(cfg, DisplayState::Day);
    TEST_ASSERT_TRUE(day.gradient);
    TEST_ASSERT_EQUAL_UINT8(40, day.ghost);
    TEST_ASSERT_EQUAL_UINT8(30, day.breathDepth);
    TEST_ASSERT_TRUE(day.transition == Transition::Falling);

    const ClockStyle night = clockStyleFor(cfg, DisplayState::Night);
    TEST_ASSERT_FALSE_MESSAGE(night.gradient, "nachts kein Verlauf");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, night.ghost, "nachts keine Geisterwoerter");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, night.breathDepth, "nachts kein Atmen");
    TEST_ASSERT_TRUE_MESSAGE(night.transition == Transition::None, "nachts keine Bewegung");
    TEST_ASSERT_TRUE(night.color == cfg.getColor(ConfigKey::NightColor));
}

// Waere die Helligkeit im Aus-Zustand 0, waere auch ein kritischer Fehler
// unsichtbar -- der ihn gerade durchbrechen soll (DESIGN 6).
static void test_off_state_keeps_brightness_for_faults() {
    Config cfg;
    cfg.set(ConfigKey::Brightness, 120);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(120, brightnessFor(cfg, DisplayState::Off),
                                    "Aus darf die Helligkeit nicht auf 0 ziehen");
    TEST_ASSERT_EQUAL_UINT8(120, brightnessFor(cfg, DisplayState::Day));
    TEST_ASSERT_EQUAL_UINT8(cfg.getU8(ConfigKey::NightBrightness),
                            brightnessFor(cfg, DisplayState::Night));
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_schema_is_consistent);
    RUN_TEST(test_key_lookup_round_trips);
    RUN_TEST(test_chime_options_are_real_animations);
    RUN_TEST(test_defaults_are_applied);
    RUN_TEST(test_values_are_clamped_never_rejected);
    RUN_TEST(test_dirty_only_on_real_change);
    RUN_TEST(test_color_decoding);
    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_partial_and_unknown_keys_are_survivable);
    RUN_TEST(test_schema_json_describes_every_item);
    RUN_TEST(test_window_wraps_over_midnight);
    RUN_TEST(test_window_without_wrap);
    RUN_TEST(test_off_beats_night);
    RUN_TEST(test_disabled_windows_never_trigger);
    RUN_TEST(test_night_profile_is_quiet);
    RUN_TEST(test_off_state_keeps_brightness_for_faults);
    return UNITY_END();
}
