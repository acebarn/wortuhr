#pragma once

#include <cstdint>

#include "wordclock/Config.h"
#include "wordclock/Frame.h"
#include "wordclock/Secrets.h"

namespace wordclock {

// Die Schnittstellen, an denen die Anwendung auf die Aussenwelt trifft.
//
// Alles darueber ist portabel und laeuft unveraendert auf dem ESP8266 wie auf
// dem Mac. Der Simulator ist deshalb keine Nachbildung der Firmware, sondern
// dieselbe Firmware mit anderen Adaptern -- sonst wuerde er mit der Zeit
// etwas anderes zeigen als das Geraet tut, und genau das soll er ja
// ausschliessen.

struct IStrip {
    virtual ~IStrip() = default;
    virtual void show(const Frame& frame) = 0;
};

// Zeit und Uhrzeit. Getrennt von der Systemzeit, damit der Simulator raffen
// kann -- ein Tag-Nacht-Wechsel soll nicht acht Stunden dauern.
struct IClock {
    virtual ~IClock() = default;

    // Monotone Millisekunden seit Start. Grundlage aller Bewegung.
    virtual uint32_t nowMs() = 0;

    virtual bool everSynced() const = 0;
    virtual uint32_t secondsSinceSync() const = 0;
    virtual void localHm(uint8_t& hours, uint8_t& minutes) const = 0;
};

struct INetwork {
    virtual ~INetwork() = default;
    virtual bool connected() const = 0;
    virtual bool apActive() const = 0;
    virtual int rssi() const = 0;
    virtual const char* ip() const { return ""; }

    // Neue Zugangsdaten uebernehmen und sofort versuchen. Ohne das muesste man
    // nach dem Eintippen raten, ob sie stimmen.
    virtual void applyCredentials(const char* ssid, const char* pass) = 0;
};

struct IStorage {
    virtual ~IStorage() = default;
    virtual bool ok() const = 0;
    virtual bool load(Config& cfg) = 0;
    virtual bool save(Config& cfg) = 0;

    // Zugangsdaten liegen in einer eigenen Datei: ein Werksreset der Anzeige
    // soll die WLAN-Verbindung nicht mitnehmen -- und umgekehrt.
    virtual bool loadSecrets(Secrets& sec) = 0;
    virtual bool saveSecrets(Secrets& sec) = 0;
};

// Rohes MQTT. Kennt weder Topics noch Nutzlasten der Uhr -- was gesendet und
// wie Eingehendes gedeutet wird, entscheidet MqttService.
struct IMqttTransport {
    virtual ~IMqttTransport() = default;

    struct Listener {
        virtual ~Listener() = default;
        virtual void onMqttMessage(const char* topic, const char* payload) = 0;
        virtual void onMqttConnected() = 0;
    };

    virtual bool enabled() const = 0;
    virtual bool connected() const = 0;

    // Verbindungsaufbau und -pflege. Wird regelmaessig aufgerufen und darf
    // nicht blockieren.
    virtual void tick(uint32_t nowMs) = 0;

    virtual void setListener(Listener* listener) = 0;
    virtual void setWill(const char* topic, const char* payload) = 0;
    virtual bool subscribe(const char* topic) = 0;
    virtual bool publish(const char* topic, const char* payload, bool retained) = 0;

    virtual void applyBroker(const char* host, uint16_t port, const char* user,
                             const char* pass) = 0;
};

// Nur fuer die Diagnose.
struct ISystemInfo {
    virtual ~ISystemInfo() = default;
    virtual uint32_t freeHeap() const = 0;
    virtual void log(const char* line) = 0;
    virtual void restart() {}
};

struct Ports {
    IStrip* strip = nullptr;
    IClock* clock = nullptr;
    INetwork* network = nullptr;
    IStorage* storage = nullptr;
    IMqttTransport* mqtt = nullptr;
    ISystemInfo* system = nullptr;
};

}  // namespace wordclock
