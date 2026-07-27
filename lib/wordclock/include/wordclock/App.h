#pragma once

#include "wordclock/ClockRenderer.h"
#include "wordclock/Compositor.h"
#include "wordclock/DotRenderer.h"
#include "wordclock/Health.h"
#include "wordclock/MqttService.h"
#include "wordclock/Notify.h"
#include "wordclock/Ports.h"
#include "wordclock/Profiles.h"

namespace wordclock {

// Die Anwendung. Vollstaendig portabel.
//
// Firmware und Simulator fuehren denselben Code aus und unterscheiden sich nur
// in den Adaptern hinter Ports. Was hier passiert, ist damit auf dem Rechner
// pruefbar -- vorher war ausgerechnet die Hauptschleife die einzige
// ungetestete Stelle im Projekt.
class App {
public:
    struct Snapshot {
        uint8_t hours = 0;
        uint8_t minutes = 0;
        DisplayState state = DisplayState::Day;
        HealthState health;
        uint32_t currentMa = 0;
        uint32_t frames = 0;
    };

    explicit App(const Ports& ports) : ports_(ports) {}

    void begin();

    // Muss regelmaessig laufen. Zeichnet hoechstens alle kFrameIntervalMs ein
    // Bild, kuemmert sich sonst um Netz, Zeit und Speicher.
    void tick();

    Config& config() { return config_; }
    NotifyStack& notifications() { return notify_; }
    MqttService& mqtt() { return mqtt_; }
    const Snapshot& snapshot() const { return snapshot_; }
    const Frame& frame() const { return compositor_.current(); }

    // Nur fuer Tests und den Simulator.
    void setFrameInterval(uint16_t ms) { frameIntervalMs_ = ms; }

private:
    void applyStyles(DisplayState state);
    HealthInputs gatherHealth();
    void autosave(uint32_t nowMs);

    Ports ports_;
    Config config_;

    ClockRenderer clockRenderer_;
    DotRenderer dotRenderer_;
    Compositor compositor_;
    NotifyStack notify_;
    Overlay overlay_;
    MqttService mqtt_;

    Snapshot snapshot_;

    uint16_t frameIntervalMs_ = 50;
    uint32_t lastFrameMs_ = 0;
    uint32_t lastDiagMs_ = 0;

    DisplayState appliedState_ = DisplayState::Day;
    bool stylesApplied_ = false;
    uint32_t appliedRevision_ = 0xFFFFFFFFu;

    // Entprellung des Speicherns: gewartet wird auf Ruhe, nicht auf die erste
    // Aenderung -- sonst schriebe ein gezogener Schieberegler mitten im Ziehen.
    uint32_t lastChangeRevision_ = 0;
    uint32_t lastChangeMs_ = 0;
};

inline constexpr uint32_t kAutosaveQuietMs = 3000;

}  // namespace wordclock
