#pragma once

#include <cstdint>

#include "wordclock/Compositor.h"
#include "wordclock/Frame.h"
#include "wordclock/WordLayout.h"

namespace wordclock {

// Benachrichtigungen von HomeAssistant.
//
// Die Firmware kennt KEINE Ereignisse -- keine Klingel, keine Waschmaschine,
// keine Muellabfuhr. Sie kennt nur Darstellungsprimitive. Was ein Ereignis
// bedeutet, entscheidet eine HA-Automation; eine neue Idee kostet dadurch
// YAML statt eines Flashvorgangs (DESIGN 8.4).

inline constexpr uint8_t kMaxNotifications = 8;
inline constexpr uint8_t kNotifyIdLen = 16;
inline constexpr uint8_t kNotifyAnimLen = 16;
inline constexpr uint8_t kGlyphBytes = (kLetterCount + 7) / 8;  // 14

// Auch Dauerzustaende (ttl 0) verfallen, wenn der Broker lange weg ist. Sonst
// faerbt ein "Fenster offen" die Uhr wochenlang, weil das clear-Telegramm
// einmal verloren ging.
inline constexpr uint32_t kOfflineExpiryMs = 10UL * 60 * 1000;

enum class NotifyStyle : uint8_t {
    Tint,   // faerbt das Wortfeld um, ruhig
    Pulse,  // faerbt um und atmet
    Blink,  // faerbt um und blinkt
    Glyph,  // 11x10-Bitmap ueber dem Wortfeld
    Word,   // hebt ein Rasterwort hervor
    Anim,   // ersetzt die Basis-Ebene durch eine Animation
};

const char* styleName(NotifyStyle s);
bool parseStyle(const char* name, NotifyStyle& out);

// Was ueber MQTT hereinkommt.
struct NotifyRequest {
    const char* id = nullptr;  // Kanalname, zugleich Schluessel zum Loeschen
    uint8_t prio = 50;
    NotifyStyle style = NotifyStyle::Tint;
    Rgb color = kWhite;
    uint32_t ttlSeconds = 0;  // 0 = Dauerzustand

    uint8_t word = 0xFF;             // Index in enum Word, fuer Style::Word
    const uint8_t* glyph = nullptr;  // kGlyphBytes, fuer Style::Glyph
    const char* anim = nullptr;      // Name, fuer Style::Anim
};

struct Notification {
    char id[kNotifyIdLen] = {};
    uint8_t prio = 0;
    NotifyStyle style = NotifyStyle::Tint;
    Rgb color = kWhite;
    uint32_t ttlSeconds = 0;
    uint32_t startedMs = 0;
    uint8_t word = 0xFF;
    uint8_t glyph[kGlyphBytes] = {};
    char anim[kNotifyAnimLen] = {};
    uint16_t sequence = 0;  // Reihenfolge des Eintreffens, fuer Gleichstand
};

// Prioritaetsstapel mit benannten Kanaelen.
//
// Dauerzustaende liegen unten, kurze Ereignisse legen sich darueber und fallen
// nach Ablauf von selbst wieder ab -- der Dauerzustand ist danach automatisch
// wieder sichtbar. Sichtbar ist immer nur der oberste Eintrag; Mischen wurde
// verworfen, weil gemischte Signalfarben eine dritte Farbe ergeben, die nichts
// mehr bedeutet.
class NotifyStack {
public:
    // Legt an oder ersetzt den gleichnamigen Kanal. Ein erneutes Senden
    // frischt die Restlaufzeit auf -- so haelt HA einen Dauermodus am Leben.
    // Gibt false zurueck, wenn der Stapel voll ist und nichts Schwaecheres
    // verdraengt werden konnte.
    bool push(const NotifyRequest& req, uint32_t nowMs);

    bool clear(const char* id);
    void clearAll();

    // Ablauf und Selbstheilung. Muss regelmaessig laufen.
    void tick(uint32_t nowMs, bool mqttConnected);

    uint8_t size() const { return count_; }
    const Notification* top() const;
    const Notification* find(const char* id) const;

    // Auf die Ausgabe anwenden. `breathingActive` meldet, ob das Wortfeld
    // ohnehin im Sekundentakt atmet -- dann weicht Pulse auf Blink aus, weil
    // es sonst nicht mehr vom Grundrhythmus zu trennen waere (DESIGN 7.1).
    void apply(Modifiers& mod, Overlay& ov, uint32_t nowMs, bool breathingActive) const;

    // Name der aktiven Animation oder nullptr. Der Aufrufer ersetzt damit die
    // Basis-Ebene.
    const char* activeAnimation() const;

private:
    int8_t indexOf(const char* id) const;
    int8_t weakestIndex() const;

    Notification items_[kMaxNotifications];
    uint8_t count_ = 0;
    uint16_t nextSequence_ = 1;
    uint32_t lastMqttSeenMs_ = 0;
    bool mqttSeen_ = false;
};

}  // namespace wordclock
