#pragma once

#include <cstdint>

#include "wordclock/MqttContract.h"
#include "wordclock/Notify.h"
#include "wordclock/Ports.h"

namespace wordclock {

// Was ueber MQTT gesagt und gehoert wird.
//
// Portabel: kennt keinen Broker, nur IMqttTransport. Dadurch laeuft dieselbe
// Protokolllogik auf dem Geraet (PubSubClient) und auf dem Rechner
// (libmosquitto) -- und liegt im Test unter Kontrolle.
class MqttService : public IMqttTransport::Listener {
public:
    struct Stats {
        uint16_t discoverySent = 0;
        uint16_t discoveryFailed = 0;
        uint16_t statePublished = 0;
        uint16_t commandsApplied = 0;
        uint16_t commandsRejected = 0;
        uint16_t commandsClamped = 0;
        uint16_t notifyApplied = 0;
        uint16_t notifyRejected = 0;
    };

    struct Diagnostics {
        const char* status = "ok";
        int rssi = 0;
        uint32_t heap = 0;
        uint32_t uptimeS = 0;
        uint32_t syncAgeS = 0;
        uint32_t currentMa = 0;
    };

    void begin(IMqttTransport* transport, Config* config, NotifyStack* notify,
               ISystemInfo* system, const char* prefix = kDefaultPrefix);

    void tick(uint32_t nowMs);

    // Diagnose wird eingespeist statt selbst erhoben -- die Werte kommen aus
    // ganz unterschiedlichen Ecken.
    void publishDiagnostics(uint32_t nowMs, const Diagnostics& d);

    bool enabled() const { return transport_ && transport_->enabled(); }
    bool connected() const { return transport_ && transport_->connected(); }
    const Stats& stats() const { return stats_; }
    const char* prefix() const { return prefix_; }

    // IMqttTransport::Listener
    void onMqttMessage(const char* topic, const char* payload) override;
    void onMqttConnected() override;

private:
    void publishDiscovery();
    void publishState();
    void handleCommand(const char* key, const char* payload);
    void handleNotify(const char* payload);
    void log(const char* fmt, ...);

    IMqttTransport* transport_ = nullptr;
    Config* config_ = nullptr;
    NotifyStack* notify_ = nullptr;
    ISystemInfo* system_ = nullptr;
    const char* prefix_ = kDefaultPrefix;

    // Beim tick() gestellt, damit eingehende Telegramme -- die waehrend
    // transport_->tick() ankommen -- den richtigen Zeitpunkt sehen.
    uint32_t nowMs_ = 0;
    uint32_t publishedRevision_ = 0xFFFFFFFFu;
    uint32_t lastDiagMs_ = 0;
    Stats stats_;
};

inline constexpr uint32_t kDiagIntervalMs = 30000;

}  // namespace wordclock
