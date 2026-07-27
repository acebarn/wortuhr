#pragma once

#include <cstddef>
#include <cstdint>

#include "wordclock/Config.h"
#include "wordclock/Health.h"
#include "wordclock/Secrets.h"

namespace wordclock {

// Die Fallback-Webapp (DESIGN 8.3, 9.2).
//
// Das Geraet liefert ausschliesslich JSON -- die Seite baut ihr Formular selbst
// aus /api/schema. Damit gibt es weiterhin nur EINE Beschreibung der
// Einstellungen, und HomeAssistant und Webapp koennen nicht auseinanderlaufen.
//
// Reine Wegewahl und Nutzlast, kein Server: derselbe Code beantwortet Anfragen
// auf dem ESP8266 wie im Simulator, wo die Seite im Browser geoeffnet werden
// kann.

struct WebRequest {
    const char* method = "GET";
    const char* path = "/";
    const char* body = "";
};

struct WebResponse {
    int status = 200;
    const char* contentType = "application/json";
    char* body = nullptr;
    size_t length = 0;

    // Wird gesetzt, wenn die Seite selbst geliefert werden soll. Sie liegt
    // gross und unveraenderlich im Flash und wandert deshalb nicht durch den
    // Antwortpuffer.
    bool serveIndexPage = false;
};

// Was der Aufrufer nach einer Anfrage tun soll. Neustart und Werksreset
// duerfen nicht mitten in der Antwort passieren -- sonst sieht der Browser
// nie eine Bestaetigung.
enum class WebAction : uint8_t { None, Restart, FactoryReset, ReconnectWifi, ReconnectMqtt };

struct WebStatus {
    HealthState health;
    uint8_t hours = 0, minutes = 0;
    DisplayState displayState = DisplayState::Day;
    bool wifiConnected = false;
    bool apActive = false;
    int rssi = 0;
    bool mqttEnabled = false;
    bool mqttConnected = false;
    uint32_t heap = 0;
    uint32_t uptimeS = 0;
    const char* ip = "";
    const char* hostname = "wortuhr";
};

class WebApi {
public:
    void begin(Config* config, Secrets* secrets) {
        config_ = config;
        secrets_ = secrets;
    }

    // Der Aufrufer fuellt das vor jeder Anfrage.
    void setStatus(const WebStatus& s) { status_ = s; }

    // `buffer` nimmt die Antwort auf. Reicht er nicht, gibt es 507 statt einer
    // abgeschnittenen Antwort -- halbes JSON waere schlimmer als ein Fehler.
    WebAction handle(const WebRequest& req, WebResponse& res, char* buffer, size_t bufferSize);

    // Die eingebettete Seite. Liegt im Flash, nicht im Dateisystem: eine
    // Fallback-Seite, die erst hochgeladen werden muss, ist kein Fallback.
    static const char* indexPage();
    static size_t indexPageLength();

private:
    Config* config_ = nullptr;
    Secrets* secrets_ = nullptr;
    WebStatus status_;
};

}  // namespace wordclock
