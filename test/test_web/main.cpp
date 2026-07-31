// Host-Tests fuer die Fallback-Webapp und die Zugangsdaten.
//
//   pio test -e native

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "wordclock/WebApi.h"

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

static char g_buf[kWebBufferSize];

struct Rig {
    Config config;
    Secrets secrets;
    WebApi api;

    Rig() { api.begin(&config, &secrets); }

    WebAction call(const char* method, const char* path, const char* body, WebResponse& res) {
        WebRequest req{method, path, body};
        return api.handle(req, res, g_buf, sizeof(g_buf));
    }

    JsonDocument getJson(const char* path) {
        WebResponse res;
        call("GET", path, "", res);
        JsonDocument doc;
        deserializeJson(doc, res.body ? res.body : "{}");
        return doc;
    }
};

// --- Zugangsdaten ----------------------------------------------------------

static void test_secret_schema_matches_enum() {
    std::set<std::string> keys;
    for (uint8_t i = 0; i < kSecretCount; ++i) {
        SecretKey k;
        TEST_ASSERT_TRUE_MESSAGE(Secrets::keyIndex(kSecretSchema[i].key, k), kSecretSchema[i].key);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(i, uint8_t(k), kSecretSchema[i].key);
        TEST_ASSERT_TRUE_MESSAGE(keys.insert(kSecretSchema[i].key).second, "Schluessel doppelt");
    }
}

// Ein Kennwort darf nie im Klartext herausgegeben werden -- wer die Seite
// oeffnet, soll sehen DASS es gesetzt ist, ohne es zu erfahren.
static void test_passwords_are_masked_on_read() {
    Secrets s;
    s.set(SecretKey::WifiSsid, "MeinNetz");
    s.set(SecretKey::WifiPass, "sehrgeheim");

    TEST_ASSERT_EQUAL_STRING("MeinNetz", s.masked(SecretKey::WifiSsid));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kMaskPlaceholder, s.masked(SecretKey::WifiPass),
                                     "Kennwort darf nicht im Klartext heraus");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.masked(SecretKey::MqttPass),
                                     "ungesetztes Kennwort bleibt leer, nicht maskiert");
}

// Der Platzhalter bedeutet "unveraendert". Sonst wuerde ein Speichern der
// Seite jedes Kennwort durch seine eigene Maskierung ersetzen -- und niemand
// kaeme mehr ins WLAN.
static void test_placeholder_does_not_overwrite_password() {
    Secrets s;
    s.set(SecretKey::WifiPass, "sehrgeheim");
    s.set(SecretKey::WifiPass, kMaskPlaceholder);
    TEST_ASSERT_EQUAL_STRING("sehrgeheim", s.get(SecretKey::WifiPass));

    s.set(SecretKey::WifiPass, "neu");
    TEST_ASSERT_EQUAL_STRING("neu", s.get(SecretKey::WifiPass));
}

static void test_long_values_are_truncated_not_overflowed() {
    Secrets s;
    std::string tooLong(200, 'x');
    TEST_ASSERT_FALSE_MESSAGE(s.set(SecretKey::WifiSsid, tooLong.c_str()),
                              "Abschneiden muss gemeldet werden");
    TEST_ASSERT_TRUE(std::strlen(s.get(SecretKey::WifiSsid)) < kSecretMaxLen);
}

static void test_mqtt_port_parsing() {
    Secrets s;
    TEST_ASSERT_EQUAL_UINT16(1883, s.mqttPort());
    s.set(SecretKey::MqttPort, "8883");
    TEST_ASSERT_EQUAL_UINT16(8883, s.mqttPort());
    s.set(SecretKey::MqttPort, "quatsch");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1883, s.mqttPort(), "Unsinn faellt auf die Vorgabe zurueck");
    s.set(SecretKey::MqttPort, "99999");
    TEST_ASSERT_EQUAL_UINT16(1883, s.mqttPort());
}

// --- Wegewahl --------------------------------------------------------------

static void test_index_page_is_served_from_flash() {
    Rig r;
    WebResponse res;
    r.call("GET", "/", "", res);
    TEST_ASSERT_TRUE_MESSAGE(res.serveIndexPage, "die Seite kommt aus dem Flash");
    TEST_ASSERT_EQUAL_STRING("text/html", res.contentType);
    TEST_ASSERT_TRUE_MESSAGE(WebApi::indexPageLength() > 1000, "die Seite darf nicht leer sein");
    TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(WebApi::indexPage(), "/api/schema"),
                                 "die Seite muss ihr Formular aus dem Schema bauen");
}

static void test_unknown_path_is_404() {
    Rig r;
    WebResponse res;
    r.call("GET", "/gibtsnicht", "", res);
    TEST_ASSERT_EQUAL_INT(404, res.status);
}

// --- Schema ----------------------------------------------------------------

// Aus derselben Beschreibung wie HomeAssistant. Eine neue Zeile im Schema muss
// hier von selbst erscheinen, sonst laufen die Oberflaechen auseinander.
static void test_schema_covers_every_setting_and_secret() {
    Rig r;
    JsonDocument doc = r.getJson("/api/schema");

    TEST_ASSERT_EQUAL_UINT8(kConfigCount, doc["settings"].as<JsonArray>().size());
    TEST_ASSERT_EQUAL_UINT8(kSecretCount, doc["secrets"].as<JsonArray>().size());

    for (JsonObject o : doc["settings"].as<JsonArray>()) {
        TEST_ASSERT_TRUE(o["key"].is<const char*>());
        TEST_ASSERT_TRUE(o["label"].is<const char*>());
        TEST_ASSERT_TRUE(o["type"].is<const char*>());
        TEST_ASSERT_TRUE(o["category"].is<const char*>());
    }
}

// --- Einstellungen lesen und schreiben --------------------------------------

static void test_config_round_trip() {
    Rig r;
    WebResponse res;
    r.call("POST", "/api/config", R"({"brightness":210,"gradient":1})", res);
    TEST_ASSERT_EQUAL_INT(200, res.status);

    TEST_ASSERT_EQUAL_INT32(210, r.config.get(ConfigKey::Brightness));
    TEST_ASSERT_EQUAL_INT32(1, r.config.get(ConfigKey::Gradient));

    JsonDocument doc = r.getJson("/api/config");
    TEST_ASSERT_EQUAL_INT32(210, doc["brightness"].as<int32_t>());
}

static void test_config_post_clamps_and_reports() {
    Rig r;
    WebResponse res;
    r.call("POST", "/api/config", R"({"ghost":9999,"gibtsnicht":1,"brightness":100})", res);

    JsonDocument doc;
    deserializeJson(doc, res.body);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, doc["applied"].as<uint8_t>(), "zwei bekannte Schluessel");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, doc["clamped"].as<uint8_t>(), "ghost musste geklemmt werden");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, doc["unknown"].as<uint8_t>(), "ein unbekannter Schluessel");
    TEST_ASSERT_EQUAL_INT32(schemaOf(ConfigKey::Ghost).max, r.config.get(ConfigKey::Ghost));
}

static void test_broken_json_is_rejected() {
    Rig r;
    WebResponse res;
    r.call("POST", "/api/config", "{das ist kein json", res);
    TEST_ASSERT_EQUAL_INT(400, res.status);
}

// --- Zugangsdaten ueber die Schnittstelle ------------------------------------

static void test_secrets_never_leak_through_the_api() {
    Rig r;
    r.secrets.set(SecretKey::WifiPass, "sehrgeheim");
    r.secrets.set(SecretKey::MqttPass, "auchgeheim");
    // Das OTA-Kennwort ist das heikelste von allen: wer es kennt, schreibt
    // beliebige Firmware auf die Uhr.
    r.secrets.set(SecretKey::OtaPass, "flashgeheim");

    WebResponse res;
    r.call("GET", "/api/secrets", "", res);
    const std::string body(res.body, res.length);

    TEST_ASSERT_TRUE_MESSAGE(body.find("sehrgeheim") == std::string::npos,
                             "WLAN-Kennwort steht in der Antwort");
    TEST_ASSERT_TRUE_MESSAGE(body.find("auchgeheim") == std::string::npos,
                             "Broker-Kennwort steht in der Antwort");
    TEST_ASSERT_TRUE_MESSAGE(body.find("flashgeheim") == std::string::npos,
                             "OTA-Kennwort steht in der Antwort");
    TEST_ASSERT_TRUE(body.find(kMaskPlaceholder) != std::string::npos);
}

static void test_saving_wifi_triggers_reconnect() {
    Rig r;
    WebResponse res;
    const WebAction a =
        r.call("POST", "/api/secrets", R"({"wifi_ssid":"Neu","wifi_pass":"geheim"})", res);
    TEST_ASSERT_TRUE_MESSAGE(a == WebAction::ReconnectWifi,
                             "frische Zugangsdaten muessen sofort versucht werden");
    TEST_ASSERT_EQUAL_STRING("Neu", r.secrets.get(SecretKey::WifiSsid));
}

static void test_saving_only_broker_reconnects_mqtt() {
    Rig r;
    WebResponse res;
    const WebAction a = r.call("POST", "/api/secrets", R"({"mqtt_host":"10.0.0.5"})", res);
    TEST_ASSERT_TRUE(a == WebAction::ReconnectMqtt);
}

// Ein versehentlicher POST darf die Einrichtung nicht loeschen.
static void test_factory_reset_needs_confirmation() {
    Rig r;
    WebResponse res;

    TEST_ASSERT_TRUE(r.call("POST", "/api/factory-reset", "", res) == WebAction::None);
    TEST_ASSERT_EQUAL_INT(400, res.status);

    TEST_ASSERT_TRUE(r.call("POST", "/api/factory-reset", R"({"confirm":false})", res) ==
                     WebAction::None);
    TEST_ASSERT_EQUAL_INT(400, res.status);

    TEST_ASSERT_TRUE(r.call("POST", "/api/factory-reset", R"({"confirm":true})", res) ==
                     WebAction::FactoryReset);
    TEST_ASSERT_EQUAL_INT(200, res.status);
}

static void test_restart_only_on_post() {
    Rig r;
    WebResponse res;
    TEST_ASSERT_TRUE(r.call("GET", "/api/restart", "", res) == WebAction::None);
    TEST_ASSERT_EQUAL_INT(404, res.status);
    TEST_ASSERT_TRUE(r.call("POST", "/api/restart", "", res) == WebAction::Restart);
}

// --- Animationen -----------------------------------------------------------

// Die Liste kommt aus der Firmware, damit eine neue Animation von selbst in der
// Seite erscheint -- dieselbe Regel wie beim Einstellungs-Schema.
static void test_animation_list_matches_the_presets() {
    Rig r;
    JsonDocument doc = r.getJson("/api/animations");
    JsonArray arr = doc.as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT8(kAnimPresetCount, arr.size());

    uint8_t i = 0;
    for (JsonVariant v : arr)
        TEST_ASSERT_EQUAL_STRING(kAnimPresets[i++].name, v.as<const char*>());
}

static void test_playing_an_animation() {
    Rig r;
    WebResponse res;
    const WebAction a =
        r.call("POST", "/api/animation", R"({"name":"feuer","seconds":12})", res);

    TEST_ASSERT_TRUE(a == WebAction::PlayAnimation);
    TEST_ASSERT_EQUAL_STRING("feuer", r.api.pendingAnimation());
    TEST_ASSERT_EQUAL_UINT16(12, r.api.pendingAnimationSeconds());
}

// Ohne Frist liefe eine Probe endlos weiter, wenn man den Tab schliesst.
static void test_animation_duration_is_bounded() {
    Rig r;
    WebResponse res;

    r.call("POST", "/api/animation", R"({"name":"welle"})", res);
    TEST_ASSERT_TRUE_MESSAGE(r.api.pendingAnimationSeconds() > 0, "es muss eine Vorgabe geben");

    r.call("POST", "/api/animation", R"({"name":"welle","seconds":99999})", res);
    TEST_ASSERT_TRUE_MESSAGE(r.api.pendingAnimationSeconds() <= 300, "Dauer muss begrenzt sein");

    r.call("POST", "/api/animation", R"({"name":"welle","seconds":0})", res);
    TEST_ASSERT_TRUE_MESSAGE(r.api.pendingAnimationSeconds() >= 1, "0 Sekunden waeren sinnlos");
}

static void test_unknown_animation_is_rejected() {
    Rig r;
    WebResponse res;
    TEST_ASSERT_TRUE(r.call("POST", "/api/animation", R"({"name":"gibtsnicht"})", res) ==
                     WebAction::None);
    TEST_ASSERT_EQUAL_INT(400, res.status);
}

static void test_stopping_an_animation() {
    Rig r;
    WebResponse res;
    TEST_ASSERT_TRUE(r.call("POST", "/api/animation", R"({"stop":true})", res) ==
                     WebAction::StopAnimation);
    TEST_ASSERT_EQUAL_INT(200, res.status);
}

// --- Zustand ---------------------------------------------------------------

static void test_status_reports_health() {
    Rig r;
    WebStatus st;
    st.hours = 7;
    st.minutes = 45;
    st.wifiConnected = false;
    st.apActive = true;
    HealthInputs in;
    in.configOk = true;
    in.apActive = true;
    st.health = evaluate(in);
    r.api.setStatus(st);

    JsonDocument doc = r.getJson("/api/status");
    TEST_ASSERT_EQUAL_STRING("07:45", doc["time"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ap_mode", doc["fault"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("kritisch", doc["severity"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(4, doc["code"].as<int>());
    TEST_ASSERT_TRUE(doc["ap"].as<bool>());
    TEST_ASSERT_FALSE(doc["wifi"].as<bool>());
}

// Lieber ein Fehler als halbes JSON: eine abgeschnittene Antwort waere fuer
// den Browser unlesbar und die Ursache schwer zu finden.
static void test_too_small_buffer_reports_instead_of_truncating() {
    Rig r;
    char tiny[64];
    WebRequest req{"GET", "/api/schema", ""};
    WebResponse res;
    r.api.handle(req, res, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT_MESSAGE(507, res.status, "zu kleiner Puffer muss gemeldet werden");
}

// Der Puffer des Geraets ist die einzige Groesse, die zaehlt.
//
// Genau das ist einmal schiefgegangen: das Schema wuchs ueber die 2048 Byte
// des Geraets hinaus, waehrend Simulator (4096) und Test (3072) grosszuegiger
// waren. Ergebnis war eine leere Einstellungsseite auf der echten Uhr, mit
// gruenen Tests. Deshalb misst dieser Test gegen kWebBufferSize und verlangt
// Luft nach oben -- eine Antwort, die gerade eben passt, ist eine, die die
// naechste Einstellung sprengt.
static void test_largest_answers_fit_the_device_buffer() {
    Rig r;
    const char* paths[] = {"/api/schema", "/api/config", "/api/status", "/api/animations"};

    for (const char* path : paths) {
        WebResponse res;
        r.call("GET", path, "", res);
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, res.status, path);
        TEST_ASSERT_TRUE_MESSAGE(
            res.length + 256 < kWebBufferSize,
            msg("%s braucht %u von %u Byte -- unter 256 Byte Luft", path, unsigned(res.length),
                unsigned(kWebBufferSize)));
    }
}

// =============================================================================

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_secret_schema_matches_enum);
    RUN_TEST(test_passwords_are_masked_on_read);
    RUN_TEST(test_placeholder_does_not_overwrite_password);
    RUN_TEST(test_long_values_are_truncated_not_overflowed);
    RUN_TEST(test_mqtt_port_parsing);
    RUN_TEST(test_index_page_is_served_from_flash);
    RUN_TEST(test_unknown_path_is_404);
    RUN_TEST(test_schema_covers_every_setting_and_secret);
    RUN_TEST(test_config_round_trip);
    RUN_TEST(test_config_post_clamps_and_reports);
    RUN_TEST(test_broken_json_is_rejected);
    RUN_TEST(test_secrets_never_leak_through_the_api);
    RUN_TEST(test_saving_wifi_triggers_reconnect);
    RUN_TEST(test_saving_only_broker_reconnects_mqtt);
    RUN_TEST(test_factory_reset_needs_confirmation);
    RUN_TEST(test_restart_only_on_post);
    RUN_TEST(test_animation_list_matches_the_presets);
    RUN_TEST(test_playing_an_animation);
    RUN_TEST(test_animation_duration_is_bounded);
    RUN_TEST(test_unknown_animation_is_rejected);
    RUN_TEST(test_stopping_an_animation);
    RUN_TEST(test_status_reports_health);
    RUN_TEST(test_too_small_buffer_reports_instead_of_truncating);
    RUN_TEST(test_largest_answers_fit_the_device_buffer);
    return UNITY_END();
}
