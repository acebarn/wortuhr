// Terminal-Vorschau des Panels.
//
// Benutzt bewusst die echte Kette ClockRenderer -> Compositor, damit das
// Bild dem entspricht, was spaeter auf der Wand steht -- einschliesslich
// Ueberblendung und Strombegrenzung. Keine Nachbildung der Logik.
//
//   pio run -e preview && .pio/build/preview/program --help

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "TerminalPanel.h"
#include "wordclock/ClockRenderer.h"
#include "wordclock/Compositor.h"
#include "wordclock/DotRenderer.h"
#include "wordclock/Health.h"

using namespace wordclock;

using termpanel::cursorUp;
using termpanel::detectColorSupport;
using termpanel::kPrintedLines;
using termpanel::printPanel;

namespace {

// --- Eingabe ---------------------------------------------------------------

struct Options {
    uint8_t hours = 7;
    uint8_t minutes = 28;
    bool animate = true;
    bool loop = false;
    ClockStyle style;
    uint8_t smoothing = 128;
    uint16_t currentLimit = kDefaultCurrentLimitMa;
    HealthInputs health;
    bool panelOff = false;
};

// Ein rundum gesundes Geraet als Ausgangspunkt.
HealthInputs healthyInputs() {
    HealthInputs in;
    in.configOk = true;
    in.wifiConnected = true;
    in.apActive = false;
    in.mqttConnected = true;
    in.everSynced = true;
    in.secondsSinceSync = 60;
    return in;
}

bool applyFault(const std::string& name, HealthInputs& in) {
    in = healthyInputs();
    if (name == "ok") return true;
    if (name == "mqtt") { in.mqttConnected = false; return true; }
    if (name == "stale") { in.secondsSinceSync = kSyncWarnSeconds + 3600; return true; }
    if (name == "nowifi") { in.wifiConnected = false; return true; }
    if (name == "notime") { in.everSynced = false; return true; }
    if (name == "config") { in.configOk = false; return true; }
    if (name == "ap") { in.wifiConnected = false; in.apActive = true; return true; }
    return false;
}

void usage() {
    std::printf(R"(Wortuhr — Terminal-Vorschau

  program [HH:MM] [Optionen]

  --transition none|staggered|fadetop|falling   Vorgabe: staggered
  --ms N            Dauer des Uebergangs in ms   Vorgabe: 400
  --ghost N         Geisterwoerter 0..255        Vorgabe: 0
  --fillers         auch Fuellbuchstaben glimmen lassen
  --gradient        Farbverlauf ueber die Wortkette
  --color R,G,B     Uhrenfarbe                   Vorgabe: 255,180,60
  --to R,G,B        Zielfarbe des Verlaufs       Vorgabe: 255,90,20
  --breath N        Tiefe des Sekundenatmens     Vorgabe: 0
  --smoothing N     Ueberblendung 0..255         Vorgabe: 128
  --limit N         Strombegrenzung in mA        Vorgabe: 2500
  --static          nur ein Bild, keine Animation
  --loop            fortlaufend, 5 Minuten je Schritt

  --fault NAME      Gesundheitszustand vortaeuschen:
                      ok      ruhig, Punkte zeigen die Minute
                      mqtt    Warnung, atmet langsam
                      stale   Warnung, Sync ueber 3 Tage her
                      nowifi  kritisch, 1 Punkt blinkt rot
                      notime  kritisch, 2 rot -- Wortfeld bleibt dunkel
                      config  kritisch, 3 rot
                      ap      kritisch, 4 blau als Lauflicht
  --off             Aus-Zustand: nur Kritisches bricht durch

  --help

  Ohne Argumente: Uebergang von 07:28 auf 07:30.
)");
}

bool parseRgb(const char* s, Rgb& out) {
    int r, g, b;
    if (std::sscanf(s, "%d,%d,%d", &r, &g, &b) != 3) return false;
    out = {uint8_t(r), uint8_t(g), uint8_t(b)};
    return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };

        if (a == "--help" || a == "-h") {
            usage();
            return false;
        } else if (a == "--static") {
            o.animate = false;
        } else if (a == "--loop") {
            o.loop = true;
        } else if (a == "--off") {
            o.panelOff = true;
        } else if (a == "--fault") {
            const char* v = next();
            if (!v || !applyFault(v, o.health)) {
                std::printf("unbekannter Zustand: %s\n", v ? v : "(fehlt)");
                return false;
            }
        } else if (a == "--fillers") {
            o.style.ghostFillers = true;
        } else if (a == "--gradient") {
            o.style.gradient = true;
        } else if (a == "--transition") {
            const std::string v = next() ? argv[i] : "";
            if (v == "none") o.style.transition = Transition::None;
            else if (v == "staggered") o.style.transition = Transition::Staggered;
            else if (v == "fadetop") o.style.transition = Transition::FadeTop;
            else if (v == "falling") o.style.transition = Transition::Falling;
            else { std::printf("unbekannter Uebergang: %s\n", v.c_str()); return false; }
        } else if (a == "--ms") {
            o.style.transitionMs = uint16_t(std::atoi(next()));
        } else if (a == "--ghost") {
            o.style.ghost = uint8_t(std::atoi(next()));
        } else if (a == "--breath") {
            o.style.breathDepth = uint8_t(std::atoi(next()));
        } else if (a == "--smoothing") {
            o.smoothing = uint8_t(std::atoi(next()));
        } else if (a == "--limit") {
            o.currentLimit = uint16_t(std::atoi(next()));
        } else if (a == "--color") {
            if (!parseRgb(next(), o.style.color)) return false;
        } else if (a == "--to") {
            if (!parseRgb(next(), o.style.gradientTo)) return false;
        } else {
            int h, m;
            if (std::sscanf(a.c_str(), "%d:%d", &h, &m) == 2) {
                o.hours = uint8_t(h);
                o.minutes = uint8_t(m);
            } else {
                std::printf("unbekanntes Argument: %s\n\n", a.c_str());
                usage();
                return false;
            }
        }
    }
    return true;
}

const char* transitionName(Transition t) {
    switch (t) {
        case Transition::None: return "none";
        case Transition::Staggered: return "staggered";
        case Transition::FadeTop: return "fadetop";
        case Transition::Falling: return "falling";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    detectColorSupport();

    Options o;
    o.health = healthyInputs();
    if (!parseArgs(argc, argv, o)) return 0;

    ClockRenderer renderer;
    renderer.setStyle(o.style);

    DotRenderer dots;
    {
        DotStyle ds;
        ds.clockColor = o.style.color;
        dots.setStyle(ds);
    }
    const HealthState health = evaluate(o.health);

    Compositor comp;
    comp.setSmoothing(o.smoothing);
    comp.setCurrentLimit(o.currentLimit);

    Overlay overlay;
    overlay.clear();
    Modifiers mod;

    uint8_t h = o.hours, m = o.minutes;
    uint32_t nowMs = 100000;

    auto renderOnce = [&](bool smooth) {
        Frame base;
        base.clear();

        // Ohne je gestellte Zeit bleibt das Wortfeld dunkel -- die Uhr zeigt
        // lieber nichts als etwas Erfundenes. Im Aus-Zustand ebenso.
        if (!health.wordFieldDark && !o.panelOff) renderer.render(base, h, m, nowMs);

        dots.render(base, health, m, nowMs, o.panelOff);

        const Frame& out = smooth ? comp.step(base, overlay, mod) : comp.snap(base, overlay, mod);

        char status[200];
        std::snprintf(status, sizeof(status), "%02u:%02u   %s   %s%s   %u mA%s%s", h, m,
                      transitionName(o.style.transition), faultName(health.fault),
                      o.panelOff ? " / aus" : "", unsigned(estimateCurrentMa(out)),
                      comp.lastLimitScale() < 255 ? " (begrenzt)" : "",
                      termpanel::truecolor() ? "" : "   256 Farben");
        printPanel(out, status);
    };

    if (!o.animate) {
        nowMs += 10000;  // Uebergang als abgeschlossen betrachten
        renderOnce(false);
        return 0;
    }

    std::printf("\033[?25l");  // Cursor aus

    const int steps = o.loop ? 1000000 : 1;
    for (int s = 0; s < steps; ++s) {
        // Eine Sekunde auf dem aktuellen Stand, dann weiterschalten.
        for (int f = 0; f < 20; ++f) {
            renderOnce(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            nowMs += 50;
            cursorUp(kPrintedLines);
        }

        m = uint8_t((m + 5) % 60);
        if (m < 5) h = uint8_t((h + 1) % 24);

        // Uebergang plus Nachlauf der Ueberblendung.
        for (int f = 0; f < 30; ++f) {
            renderOnce(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            nowMs += 50;
            if (!(s == steps - 1 && f == 29)) cursorUp(kPrintedLines);
        }
    }

    std::printf("\033[?25h");  // Cursor an
    return 0;
}
