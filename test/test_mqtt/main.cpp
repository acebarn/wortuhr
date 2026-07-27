// Host-Tests fuer den MQTT-Vertrag: Topics, Wertdarstellung, Discovery.
//
//   pio test -e native

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "wordclock/MqttContract.h"

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

// --- Topics ----------------------------------------------------------------

static void test_topics() {
    char b[96];
    topicAvailability(b, sizeof(b), "wortuhr");
    TEST_ASSERT_EQUAL_STRING("wortuhr/availability", b);
    topicState(b, sizeof(b), "wortuhr");
    TEST_ASSERT_EQUAL_STRING("wortuhr/state", b);
    topicSet(b, sizeof(b), "wortuhr", "brightness");
    TEST_ASSERT_EQUAL_STRING("wortuhr/set/brightness", b);
    topicSetWildcard(b, sizeof(b), "wortuhr");
    TEST_ASSERT_EQUAL_STRING("wortuhr/set/+", b);
    topicDiscovery(b, sizeof(b), "number", "wortuhr", "brightness");
    TEST_ASSERT_EQUAL_STRING("homeassistant/number/wortuhr/brightness/config", b);
}

static void test_key_extraction_from_set_topic() {
    char key[32];

    TEST_ASSERT_TRUE(keyFromSetTopic("wortuhr/set/brightness", "wortuhr", key, sizeof(key)));
    TEST_ASSERT_EQUAL_STRING("brightness", key);

    // Alles, was nicht genau eine Ebene unter set/ liegt, wird abgewiesen --
    // sonst koennte ein fremdes Topic Werte setzen.
    TEST_ASSERT_FALSE(keyFromSetTopic("wortuhr/state", "wortuhr", key, sizeof(key)));
    TEST_ASSERT_FALSE(keyFromSetTopic("wortuhr/set/", "wortuhr", key, sizeof(key)));
    TEST_ASSERT_FALSE(keyFromSetTopic("wortuhr/set/a/b", "wortuhr", key, sizeof(key)));
    TEST_ASSERT_FALSE(keyFromSetTopic("andere/set/brightness", "wortuhr", key, sizeof(key)));
    TEST_ASSERT_FALSE(keyFromSetTopic(nullptr, "wortuhr", key, sizeof(key)));

    char tiny[4];
    TEST_ASSERT_FALSE_MESSAGE(keyFromSetTopic("wortuhr/set/brightness", "wortuhr", tiny,
                                              sizeof(tiny)),
                              "zu kleiner Puffer muss abgewiesen werden, nicht ueberlaufen");
}

// --- Wertdarstellung -------------------------------------------------------

// Jeder Schema-Eintrag muss den Rundlauf ueber seinen ganzen Wertebereich
// ueberstehen -- sonst kaeme aus HomeAssistant etwas anderes zurueck, als die
// Uhr gesendet hat.
static void test_format_parse_round_trip_over_full_range() {
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const ConfigItem& item = kSchema[i];

        int32_t probes[6] = {item.min, item.max, item.def, (item.min + item.max) / 2,
                             item.min + 1, item.max - 1};

        for (int32_t v : probes) {
            if (v < item.min || v > item.max) continue;

            char text[32];
            formatValue(item, v, text, sizeof(text));

            int32_t back = -12345;
            TEST_ASSERT_TRUE_MESSAGE(parseValue(item, text, back),
                                     msg("%s: \"%s\" nicht lesbar", item.key, text));
            TEST_ASSERT_EQUAL_INT32_MESSAGE(v, back,
                                            msg("%s: %d -> \"%s\" -> %d", item.key, int(v), text,
                                                int(back)));
        }
    }
}

static void test_color_format() {
    const ConfigItem& item = schemaOf(ConfigKey::Color);
    char b[16];
    formatValue(item, 0xFFB43C, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("FFB43C", b);

    int32_t v = 0;
    TEST_ASSERT_TRUE(parseValue(item, "FFB43C", v));
    TEST_ASSERT_EQUAL_INT32(0xFFB43C, v);
    TEST_ASSERT_TRUE_MESSAGE(parseValue(item, "#00ff80", v), "Raute und Kleinschreibung erlaubt");
    TEST_ASSERT_EQUAL_INT32(0x00FF80, v);

    TEST_ASSERT_FALSE(parseValue(item, "FFB43", v));
    TEST_ASSERT_FALSE(parseValue(item, "GGGGGG", v));
    TEST_ASSERT_FALSE(parseValue(item, "", v));
}

static void test_time_format() {
    const ConfigItem& item = schemaOf(ConfigKey::NightFrom);
    char b[16];
    formatValue(item, 22 * 60, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("22:00", b);
    formatValue(item, 7 * 60 + 5, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("07:05", b);

    int32_t v = 0;
    TEST_ASSERT_TRUE(parseValue(item, "21:45", v));
    TEST_ASSERT_EQUAL_INT32(21 * 60 + 45, v);

    TEST_ASSERT_FALSE(parseValue(item, "24:00", v));
    TEST_ASSERT_FALSE(parseValue(item, "12:60", v));
    TEST_ASSERT_FALSE(parseValue(item, "abc", v));
}

static void test_bool_accepts_ha_payloads() {
    const ConfigItem& item = schemaOf(ConfigKey::Gradient);
    int32_t v = -1;
    for (const char* on : {"ON", "on", "1", "true"}) {
        TEST_ASSERT_TRUE_MESSAGE(parseValue(item, on, v), on);
        TEST_ASSERT_EQUAL_INT32(1, v);
    }
    for (const char* off : {"OFF", "off", "0", "false"}) {
        TEST_ASSERT_TRUE_MESSAGE(parseValue(item, off, v), off);
        TEST_ASSERT_EQUAL_INT32(0, v);
    }
    TEST_ASSERT_FALSE(parseValue(item, "vielleicht", v));
}

// HomeAssistant schickt fuer number-Entitaeten gern "90.0".
static void test_number_accepts_decimal_from_ha() {
    const ConfigItem& item = schemaOf(ConfigKey::Brightness);
    int32_t v = 0;
    TEST_ASSERT_TRUE(parseValue(item, "90.0", v));
    TEST_ASSERT_EQUAL_INT32(90, v);
    TEST_ASSERT_TRUE(parseValue(item, "90", v));
    TEST_ASSERT_EQUAL_INT32(90, v);
    TEST_ASSERT_FALSE(parseValue(item, "hell", v));
}

static void test_choice_uses_names_not_indices() {
    const ConfigItem& item = schemaOf(ConfigKey::Transition);
    char b[24];
    formatValue(item, 3, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("falling", b);

    int32_t v = 0;
    TEST_ASSERT_TRUE(parseValue(item, "fadetop", v));
    TEST_ASSERT_EQUAL_INT32(2, v);
    TEST_ASSERT_FALSE_MESSAGE(parseValue(item, "2", v), "Index als Text ist kein gueltiger Name");
    TEST_ASSERT_FALSE(parseValue(item, "gibtsnicht", v));
}

// --- Discovery -------------------------------------------------------------

static void test_every_setting_gets_a_valid_discovery_payload() {
    DeviceInfo dev;
    std::set<std::string> uniqueIds;

    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const ConfigItem& item = kSchema[i];

        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        buildConfigDiscovery(o, item, "wortuhr", dev);

        TEST_ASSERT_TRUE_MESSAGE(o["name"].is<const char*>(), item.key);
        TEST_ASSERT_TRUE_MESSAGE(o["uniq_id"].is<const char*>(), item.key);
        TEST_ASSERT_TRUE_MESSAGE(o["cmd_t"].is<const char*>(), item.key);
        TEST_ASSERT_TRUE_MESSAGE(o["avty_t"].is<const char*>(), item.key);
        TEST_ASSERT_TRUE_MESSAGE(o["dev"].is<JsonObject>(), item.key);

        // Ein state_topic ist Pflicht: nur damit arbeitet HA
        // nicht-optimistisch und zeigt ausschliesslich Bestaetigtes
        // (DESIGN 8.1).
        TEST_ASSERT_TRUE_MESSAGE(o["stat_t"].is<const char*>(),
                                 msg("%s ohne state_topic -- HA waere optimistisch", item.key));
        TEST_ASSERT_TRUE_MESSAGE(o["val_tpl"].is<const char*>(), item.key);

        TEST_ASSERT_TRUE_MESSAGE(uniqueIds.insert(o["uniq_id"].as<std::string>()).second,
                                 msg("%s: uniq_id doppelt", item.key));

        char expected[96];
        topicSet(expected, sizeof(expected), "wortuhr", item.key);
        TEST_ASSERT_EQUAL_STRING(expected, o["cmd_t"].as<const char*>());

        if (item.type == ConfigType::Number) {
            TEST_ASSERT_EQUAL_INT32_MESSAGE(item.min, o["min"].as<int32_t>(), item.key);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(item.max, o["max"].as<int32_t>(), item.key);
        }
        if (item.type == ConfigType::Choice) {
            TEST_ASSERT_TRUE_MESSAGE(o["ops"].is<JsonArray>(), item.key);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(item.optionCount, o["ops"].as<JsonArray>().size(),
                                            item.key);
        }
    }
}

static void test_components_are_known_types() {
    // Bewusst nur Typen, die in der MQTT-Integration lange stabil sind.
    const std::set<std::string> allowed = {"switch", "number", "select", "text"};
    for (uint8_t i = 0; i < kConfigCount; ++i) {
        const char* c = componentFor(kSchema[i].type);
        TEST_ASSERT_TRUE_MESSAGE(allowed.count(c) == 1, msg("%s -> %s", kSchema[i].key, c));
    }
}

static void test_diag_sensors_are_diagnostic() {
    DeviceInfo dev;
    for (uint8_t i = 0; i < kDiagSensorCount; ++i) {
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        buildDiagDiscovery(o, kDiagSensors[i], "wortuhr", dev);

        TEST_ASSERT_EQUAL_STRING_MESSAGE("diagnostic", o["ent_cat"].as<const char*>(),
                                         kDiagSensors[i].key);
        TEST_ASSERT_TRUE_MESSAGE(o["stat_t"].is<const char*>(), kDiagSensors[i].key);
        TEST_ASSERT_FALSE_MESSAGE(o["cmd_t"].is<const char*>(),
                                  "ein Diagnosewert darf nicht setzbar sein");
    }
}

// Ein Discovery-Telegramm muss in den Sendepuffer passen. PubSubClient
// verwirft stillschweigend, was zu gross ist -- die Entitaet fehlt dann
// einfach in HomeAssistant, ohne Fehlermeldung.
static void test_payloads_fit_the_mqtt_buffer() {
    constexpr size_t kBufferLimit = 1024;
    DeviceInfo dev;
    dev.version = "1.0.0";

    size_t worst = 0;
    const char* worstKey = "";

    for (uint8_t i = 0; i < kConfigCount; ++i) {
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        buildConfigDiscovery(o, kSchema[i], "wortuhr", dev);
        const size_t len = measureJson(doc);
        if (len > worst) { worst = len; worstKey = kSchema[i].key; }
        TEST_ASSERT_TRUE_MESSAGE(len < kBufferLimit,
                                 msg("%s: %u Byte", kSchema[i].key, unsigned(len)));
    }
    std::printf("    groesstes Discovery-Telegramm: %s, %u Byte\n", worstKey, unsigned(worst));
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_topics);
    RUN_TEST(test_key_extraction_from_set_topic);
    RUN_TEST(test_format_parse_round_trip_over_full_range);
    RUN_TEST(test_color_format);
    RUN_TEST(test_time_format);
    RUN_TEST(test_bool_accepts_ha_payloads);
    RUN_TEST(test_number_accepts_decimal_from_ha);
    RUN_TEST(test_choice_uses_names_not_indices);
    RUN_TEST(test_every_setting_gets_a_valid_discovery_payload);
    RUN_TEST(test_components_are_known_types);
    RUN_TEST(test_diag_sensors_are_diagnostic);
    RUN_TEST(test_payloads_fit_the_mqtt_buffer);
    return UNITY_END();
}
