// Wortuhr — Simulator.
//
// Fuehrt wordclock::App unveraendert aus, nur mit Adaptern fuer den Rechner:
// Panel im Terminal, Uhrzeit im Zeitraffer, Konfiguration als Datei, MQTT ueber
// einen echten Broker. Keine Nachbildung der Firmware -- dieselbe Firmware.
//
//   brew services start mosquitto    (oder: mosquitto -v)
//   pio run -e sim && .pio/build/sim/program --help

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "HttpServer.h"
#include "MosquittoTransport.h"
#include "SimAdapters.h"
#include "TerminalPanel.h"
#include "wordclock/App.h"

using namespace wordclock;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

struct Options {
    std::string broker = "localhost";
    int port = 1883;
    std::string user, pass;
    std::string configPath = "sim-config.json";
    double speed = 60.0;  // Wanduhr-Beschleunigung
    uint8_t startHour = 7, startMinute = 28;
    bool synced = true;
    uint32_t syncAge = 60;
    bool wifi = true;
    bool ap = false;
    bool headless = false;
    int fps = 10;
    int httpPort = 8080;
};

void usage() {
    std::printf(R"(Wortuhr — Simulator

  program [Optionen]

  --broker HOST     Broker, leer = MQTT aus     Vorgabe: localhost
  --port N                                      Vorgabe: 1883
  --user / --pass   Zugangsdaten
  --config PATH     Konfigurationsdatei         Vorgabe: sim-config.json

  --speed N         Wanduhr-Beschleunigung      Vorgabe: 60
                    (Bewegung bleibt in Echtzeit)
  --start HH:MM     Startzeit                   Vorgabe: 07:28
  --fps N           Bildrate der Ausgabe        Vorgabe: 10

  --no-wifi         WLAN als getrennt melden
  --ap              AP-Modus melden
  --no-sync         nie synchronisiert (Wortfeld bleibt dunkel)
  --sync-age N      Sekunden seit letztem Sync
  --headless        kein Panel im Terminal, nur Protokoll
  --http N          Port der Weboberflaeche      Vorgabe: 8080

  --help

Steuern laesst sich der laufende Simulator ueber MQTT, genau wie die Uhr:

  mosquitto_pub -t wortuhr/set/brightness -m 200
  mosquitto_pub -t wortuhr/set/color -m 00FF80
  mosquitto_pub -t wortuhr/set/ghost -m 30
  mosquitto_pub -t wortuhr/notify \
      -m '{"id":"fenster","style":"tint","color":[0,180,255],"ttl":20}'

Im Browser:

  http://localhost:8080/panel     Wortuhr-Ansicht wie an der Wand
  http://localhost:8080/gallery   alle Animationen nebeneinander, live
  http://localhost:8080/          Fallback-Konfigurationsseite

Mitlesen, was HomeAssistant empfangen wuerde:

  mosquitto_sub -v -t 'homeassistant/#' -t 'wortuhr/#'
)");
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };

        if (a == "--help" || a == "-h") { usage(); return false; }
        else if (a == "--broker") o.broker = next();
        else if (a == "--port") o.port = std::atoi(next());
        else if (a == "--user") o.user = next();
        else if (a == "--pass") o.pass = next();
        else if (a == "--config") o.configPath = next();
        else if (a == "--speed") o.speed = std::atof(next());
        else if (a == "--fps") o.fps = std::atoi(next());
        else if (a == "--no-wifi") o.wifi = false;
        else if (a == "--ap") { o.ap = true; o.wifi = false; }
        else if (a == "--no-sync") o.synced = false;
        else if (a == "--sync-age") o.syncAge = uint32_t(std::atol(next()));
        else if (a == "--headless") o.headless = true;
        else if (a == "--http") o.httpPort = std::atoi(next());
        else if (a == "--start") {
            int h = 7, m = 28;
            std::sscanf(next(), "%d:%d", &h, &m);
            o.startHour = uint8_t(h);
            o.startMinute = uint8_t(m);
        } else {
            std::printf("unbekanntes Argument: %s\n\n", a.c_str());
            usage();
            return false;
        }
    }
    return true;
}

const char* stateName(DisplayState s) {
    switch (s) {
        case DisplayState::Day: return "tag";
        case DisplayState::Night: return "nacht";
        case DisplayState::Off: return "aus";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) return 0;

    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // zeilenweise, sonst haengt alles im Puffer
    std::signal(SIGINT, onSignal);
    termpanel::detectColorSupport();

    sim::TerminalStrip strip;
    sim::SimClock clock;
    sim::SimNetwork network;
    sim::FileStorage storage(o.configPath);
    sim::SimSystem system;
    sim::MosquittoTransport mqtt;

    clock.begin(o.startHour, o.startMinute, o.speed, o.synced);
    clock.setSyncAge(o.syncAge);
    network.set(o.wifi, o.ap);
    mqtt.begin(o.broker, o.port, o.user, o.pass, "wortuhr-sim");

    // Die Kommandozeile ist Erstbefuellung, genau wie secrets.h auf dem Geraet.
    // Ohne das wuerde App::begin() die leeren gespeicherten Zugangsdaten in den
    // Transport schieben und MQTT damit abschalten.
    Secrets seed;
    seed.set(SecretKey::WifiSsid, "Simulator");
    seed.set(SecretKey::MqttHost, o.broker.c_str());
    seed.set(SecretKey::MqttUser, o.user.c_str());
    seed.set(SecretKey::MqttPass, o.pass.c_str());
    {
        char portBuf[8];
        std::snprintf(portBuf, sizeof(portBuf), "%d", o.port);
        seed.set(SecretKey::MqttPort, portBuf);
    }

    Ports ports;
    ports.strip = &strip;
    ports.clock = &clock;
    ports.network = &network;
    ports.storage = &storage;
    ports.mqtt = &mqtt;
    ports.system = &system;

    App app(ports);
    app.begin(&seed);

    sim::HttpServer http;
    static App* appPtr = &app;
    const bool httpOk = http.begin(o.httpPort, &app.web(), &strip.last(),
                                   [](wordclock::WebAction a, void*) { appPtr->applyWebAction(a); });
    if (httpOk)
        std::printf("Weboberflaeche: http://localhost:%d/panel  ·  /gallery  ·  /\n",
                    o.httpPort);
    else
        std::printf("Weboberflaeche konnte Port %d nicht belegen\n", o.httpPort);

    std::printf("Simulator laeuft. Broker: %s  Zeitraffer: %.0fx  Strg-C beendet.\n",
                o.broker.empty() ? "(aus)" : o.broker.c_str(), o.speed);

    const auto frameGap = std::chrono::milliseconds(1000 / (o.fps > 0 ? o.fps : 10));
    bool drewOnce = false;
    size_t shownLogs = 0;

    while (!g_stop) {
        clock.poll();
        app.tick();
        app.refreshWebStatus();
        http.poll(clock.nowMs());

        // Protokollzeilen erscheinen oberhalb des Panels, damit sie beim
        // Ueberzeichnen nicht verlorengehen.
        if (shownLogs < system.logs().size()) {
            if (drewOnce && !o.headless) {
                termpanel::cursorUp(termpanel::kPrintedLines);
                for (int i = 0; i < termpanel::kPrintedLines; ++i) std::printf("\033[2K\n");
                termpanel::cursorUp(termpanel::kPrintedLines);
                drewOnce = false;
            }
            for (; shownLogs < system.logs().size(); ++shownLogs)
                std::printf("  %s\n", system.logs()[shownLogs].c_str());
        }

        if (!o.headless) {
            const App::Snapshot& s = app.snapshot();
            char status[220];
            std::snprintf(status, sizeof(status),
                          "%02u:%02u  %s  %s  wlan=%s  mqtt=%s  %umA  %ux Zeitraffer", s.hours,
                          s.minutes, stateName(s.state), faultName(s.health.fault),
                          network.connected() ? "ja" : "nein", mqtt.connected() ? "ja" : "nein",
                          unsigned(s.currentMa), unsigned(o.speed));

            if (drewOnce) termpanel::cursorUp(termpanel::kPrintedLines);
            termpanel::printPanel(strip.last(), status);
            drewOnce = true;
        }

        std::this_thread::sleep_for(frameGap);
    }

    std::printf("\nBeendet nach %u Bildern.\n", unsigned(app.snapshot().frames));
    return 0;
}
