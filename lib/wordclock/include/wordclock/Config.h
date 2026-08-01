#pragma once

#include <cstdint>

#include "wordclock/Color.h"

namespace wordclock {

// Die eine Beschreibung aller Einstellungen (DESIGN 8.3).
//
// Aus ihr entstehen Persistenz, Validierung, HA-Discovery und das Formular der
// Webapp. Eine neue Einstellung ist eine Zeile in kSchema -- danach existiert
// sie ueberall. Divergenz zwischen den Oberflaechen wird dadurch strukturell
// unmoeglich, nicht durch Disziplin.
//
// NUR SKALARE. Alles hier passt in ein int32: Wahrheitswerte, Zahlen, Farben
// (0xRRGGBB), Auswahlen (Index) und Uhrzeiten (Minuten seit Mitternacht).
// Zeichenketten -- Broker-Adresse, WLAN-Zugangsdaten -- brauchen eigene
// Behandlung (Geheimnisse gehoeren nicht in die HA-Discovery) und kommen
// zusammen mit MQTT und der Webapp.

enum class ConfigType : uint8_t {
    Bool,
    Number,  // min..max
    Color,   // 0xRRGGBB
    Choice,  // Index in options
    TimeOfDay,  // Minuten seit Mitternacht, 0..1439
};

enum class ConfigKey : uint8_t {
    Brightness,
    Color,
    Gradient,
    GradientTo,
    Transition,
    TransitionMs,
    Ghost,
    GhostFillers,
    BreathDepth,
    Smoothing,
    CurrentLimit,

    NightEnabled,
    NightFrom,
    NightTo,
    NightBrightness,
    NightColor,

    OffEnabled,
    OffFrom,
    OffTo,

    ChimeEnabled,
    ChimeStyle,
    ChimeSeconds,

    Ambient,

    Count
};

inline constexpr uint8_t kConfigCount = uint8_t(ConfigKey::Count);

struct ConfigItem {
    const char* key;       // stabiler Bezeichner, auch MQTT-Topic-Segment
    const char* label;     // Anzeigetext fuer Webapp und HomeAssistant
    const char* category;  // Gruppierung im Formular
    ConfigType type;
    int32_t min;
    int32_t max;
    int32_t def;
    const char* const* options;  // nur bei Choice
    uint8_t optionCount;
};

inline constexpr const char* kTransitionOptions[] = {"none", "staggered", "fadetop", "falling"};

// Muss zu kAnimPresets passen -- ein Test haelt das fest.
inline constexpr const char* kChimeOptions[] = {
    "matrix",     "nordlicht", "silvester", "sonnenaufgang", "feuer",    "welle",  "tropfen",
    "plasma",     "regenbogen", "wirbel",   "komet",         "konfetti", "ozean",  "glut"};
inline constexpr uint8_t kChimeOptionCount = sizeof(kChimeOptions) / sizeof(kChimeOptions[0]);

// Reihenfolge muss zu enum ConfigKey passen. Ein Test prueft das.
inline constexpr ConfigItem kSchema[kConfigCount] = {
    {"brightness", "Helligkeit", "anzeige", ConfigType::Number, 5, 255, 90, nullptr, 0},
    {"color", "Farbe", "anzeige", ConfigType::Color, 0, 0xFFFFFF, 0xFFB43C, nullptr, 0},
    {"gradient", "Farbverlauf", "anzeige", ConfigType::Bool, 0, 1, 0, nullptr, 0},
    {"gradient_to", "Verlauf-Zielfarbe", "anzeige", ConfigType::Color, 0, 0xFFFFFF, 0xFF5A14,
     nullptr, 0},
    {"transition", "Uebergang", "anzeige", ConfigType::Choice, 0, 3, 1, kTransitionOptions, 4},
    {"transition_ms", "Uebergangsdauer", "anzeige", ConfigType::Number, 0, 2000, 400, nullptr, 0},
    {"ghost", "Geisterwoerter", "anzeige", ConfigType::Number, 0, 120, 0, nullptr, 0},
    {"ghost_fillers", "Fuellbuchstaben mitglimmen", "anzeige", ConfigType::Bool, 0, 1, 0, nullptr,
     0},
    {"breath_depth", "Sekundenatmen", "anzeige", ConfigType::Number, 0, 80, 0, nullptr, 0},
    {"smoothing", "Ueberblendung", "anzeige", ConfigType::Number, 8, 255, 128, nullptr, 0},
    {"current_limit", "Strombegrenzung mA", "anzeige", ConfigType::Number, 200, 6000, 2500,
     nullptr, 0},

    {"night_enabled", "Nachtmodus", "nacht", ConfigType::Bool, 0, 1, 1, nullptr, 0},
    {"night_from", "Nacht ab", "nacht", ConfigType::TimeOfDay, 0, 1439, 22 * 60, nullptr, 0},
    {"night_to", "Nacht bis", "nacht", ConfigType::TimeOfDay, 0, 1439, 7 * 60, nullptr, 0},
    {"night_brightness", "Helligkeit nachts", "nacht", ConfigType::Number, 3, 255, 20, nullptr, 0},
    {"night_color", "Farbe nachts", "nacht", ConfigType::Color, 0, 0xFFFFFF, 0xFF7828, nullptr, 0},

    {"off_enabled", "Abschaltzeit", "aus", ConfigType::Bool, 0, 1, 0, nullptr, 0},
    {"off_from", "Aus ab", "aus", ConfigType::TimeOfDay, 0, 1439, 23 * 60, nullptr, 0},
    {"off_to", "Aus bis", "aus", ConfigType::TimeOfDay, 0, 1439, 6 * 60, nullptr, 0},

    {"chime_enabled", "Stundenschlag", "stunde", ConfigType::Bool, 0, 1, 0, nullptr, 0},
    {"chime_style", "Animation", "stunde", ConfigType::Choice, 0, kChimeOptionCount - 1, 6,
     kChimeOptions, kChimeOptionCount},
    {"chime_seconds", "Dauer", "stunde", ConfigType::Number, 1, 15, 3, nullptr, 0},

    {"ambient", "Ambient-Modus", "ambient", ConfigType::Bool, 0, 1, 0, nullptr, 0},
};

constexpr const ConfigItem& schemaOf(ConfigKey k) { return kSchema[uint8_t(k)]; }

// Werte mit Validierung. Ungueltiges wird geklemmt, nie uebernommen.
class Config {
public:
    Config() { reset(); }

    void reset();

    int32_t get(ConfigKey k) const { return values_[uint8_t(k)]; }
    bool getBool(ConfigKey k) const { return values_[uint8_t(k)] != 0; }
    uint8_t getU8(ConfigKey k) const { return uint8_t(values_[uint8_t(k)]); }
    uint16_t getU16(ConfigKey k) const { return uint16_t(values_[uint8_t(k)]); }
    Rgb getColor(ConfigKey k) const;

    // Klemmt auf den erlaubten Bereich. Gibt false zurueck, wenn dabei etwas
    // veraendert werden musste -- der Aufrufer kann das melden.
    bool set(ConfigKey k, int32_t value);
    bool setByName(const char* key, int32_t value);

    static int32_t clampTo(const ConfigItem& item, int32_t value);
    static bool keyIndex(const char* key, ConfigKey& out);

    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    // Zaehlt jede tatsaechliche Wertaenderung. Erlaubt dem Speicheradapter,
    // auf Ruhe zu warten statt auf die erste Aenderung -- sonst schriebe ein
    // gezogener Schieberegler mitten im Ziehen in den Flash.
    uint32_t revision() const { return revision_; }

private:
    int32_t values_[kConfigCount] = {};
    bool dirty_ = false;
    uint32_t revision_ = 0;
};

// --- Darstellungszustand ---------------------------------------------------

enum class DisplayState : uint8_t { Day, Night, Off };

// Welcher Zustand gilt zur angegebenen Uhrzeit?
//
// Die Fenster duerfen ueber Mitternacht laufen (22:00 bis 07:00) -- das ist der
// Normalfall und die klassische Fehlerquelle. Aus schlaegt Nacht.
DisplayState displayStateFor(const Config& cfg, uint8_t hours, uint8_t minutes);

// Liegt `minute` im Fenster [from, to)? Behandelt den Umlauf ueber Mitternacht.
bool inWindow(uint16_t minute, uint16_t from, uint16_t to);

}  // namespace wordclock
