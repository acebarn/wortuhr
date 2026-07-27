#pragma once

#include <mosquitto.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "wordclock/Ports.h"

namespace sim {

// MQTT ueber libmosquitto -- ein ECHTER Broker, kein Attrappen-Transport.
//
// Damit laufen Discovery-Telegramme, Zustandsmeldungen und Befehle ueber den
// tatsaechlichen Draht. Was HomeAssistant empfangen wuerde, laesst sich mit
// mosquitto_sub danebenlegen und vergleichen.
//
// Die Protokolllogik darueber ist dieselbe wie auf dem Geraet: beide fuettern
// wordclock::MqttService, nur der Transport unterscheidet sich.
class MosquittoTransport : public wordclock::IMqttTransport {
public:
    bool begin(const std::string& host, int port, const std::string& user,
               const std::string& pass, const std::string& clientId) {
        host_ = host;
        port_ = port;
        user_ = user;
        pass_ = pass;
        enabled_ = !host.empty();

        // Client immer anlegen: der Broker kann spaeter ueber applyBroker
        // kommen, und dann muss das Objekt schon stehen.
        mosquitto_lib_init();
        mosq_ = mosquitto_new(clientId.c_str(), /*clean_session=*/true, this);
        if (!mosq_) {
            std::printf("[mqtt] mosquitto_new fehlgeschlagen\n");
            enabled_ = false;
            return false;
        }

        if (!user_.empty())
            mosquitto_username_pw_set(mosq_, user_.c_str(),
                                      pass_.empty() ? nullptr : pass_.c_str());

        mosquitto_connect_callback_set(mosq_, &MosquittoTransport::onConnectCb);
        mosquitto_message_callback_set(mosq_, &MosquittoTransport::onMessageCb);
        mosquitto_disconnect_callback_set(mosq_, &MosquittoTransport::onDisconnectCb);
        return true;
    }

    ~MosquittoTransport() override {
        if (mosq_) {
            mosquitto_disconnect(mosq_);
            mosquitto_destroy(mosq_);
        }
        if (enabled_) mosquitto_lib_cleanup();
    }

    void applyBroker(const char* host, uint16_t port, const char* user,
                     const char* pass) override {
        if (host && *host && host_ != host) {
            std::printf("[mqtt] Broker gewechselt: %s:%u\n", host, port);
        }
        if (host) host_ = host;
        if (user) user_ = user;
        if (pass) pass_ = pass;
        port_ = port;
        enabled_ = !host_.empty();
    }

    bool enabled() const override { return enabled_; }
    bool connected() const override { return connected_; }
    void setListener(Listener* l) override { listener_ = l; }

    void setWill(const char* topic, const char* payload) override {
        if (!mosq_ || !topic) return;
        mosquitto_will_set(mosq_, topic, int(std::strlen(payload ? payload : "")), payload, 0,
                           /*retain=*/true);
    }

    bool subscribe(const char* topic) override {
        if (!connected_) return false;
        return mosquitto_subscribe(mosq_, nullptr, topic, 0) == MOSQ_ERR_SUCCESS;
    }

    bool publish(const char* topic, const char* payload, bool retained) override {
        if (!connected_) return false;
        const int rc = mosquitto_publish(mosq_, nullptr, topic, int(std::strlen(payload)),
                                         payload, 0, retained);
        return rc == MOSQ_ERR_SUCCESS;
    }

    void tick(uint32_t nowMs) override {
        if (!enabled_ || !mosq_) return;

        // Wichtig: mosquitto_connect meldet nur, dass der Verbindungsaufbau
        // angestossen wurde. Das CONNACK kommt erst im naechsten
        // mosquitto_loop, und erst dessen Callback setzt connected_. Ohne
        // eigenen "verbindet gerade"-Zustand riefe jeder Tick bis dahin
        // erneut connect auf -- das ergab eine Schleife, in der die Uhr
        // dutzendfach ihre Verfuegbarkeit meldete und nie zum Senden der
        // Discovery-Telegramme kam.
        if (!connected_ && !connecting_) {
            if (nextAttemptMs_ != 0 && int32_t(nowMs - nextAttemptMs_) < 0) return;

            const int rc = mosquitto_connect(mosq_, host_.c_str(), port_, /*keepalive=*/30);
            if (rc != MOSQ_ERR_SUCCESS) {
                if (++attempts_ > 250) attempts_ = 250;
                const uint32_t waitMs = attempts_ <= 3 ? 2000 : 10000;
                nextAttemptMs_ = nowMs + waitMs;
                std::printf("[mqtt] Verbindung zu %s:%d fehlgeschlagen (%s), erneut in %u s\n",
                            host_.c_str(), port_, mosquitto_strerror(rc),
                            unsigned(waitMs / 1000));
                return;
            }
            connecting_ = true;
            connectDeadlineMs_ = nowMs + 5000;
        }

        // Bleibt das CONNACK aus, nicht ewig warten.
        if (connecting_ && !connected_ && int32_t(nowMs - connectDeadlineMs_) >= 0) {
            connecting_ = false;
            nextAttemptMs_ = nowMs + 2000;
            std::printf("[mqtt] keine Antwort vom Broker, neuer Versuch\n");
            return;
        }

        // Callbacks laufen dadurch auf diesem Thread -- kein Nebenlaeufer, der
        // sich mit der Hauptschleife um die Konfiguration streitet.
        const int rc = mosquitto_loop(mosq_, /*timeout=*/0, /*max_packets=*/1);
        if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
            connected_ = false;
            connecting_ = false;
            nextAttemptMs_ = nowMs + 2000;
        }
    }

private:
    void handleConnect(int rc) {
        if (rc != 0) {
            std::printf("[mqtt] abgelehnt: %s\n", mosquitto_connack_string(rc));
            return;
        }
        connected_ = true;
        connecting_ = false;
        std::printf("[mqtt] verbunden mit %s:%d\n", host_.c_str(), port_);
        if (listener_) listener_->onMqttConnected();
    }

    void handleMessage(const struct mosquitto_message* msg) {
        if (!listener_ || !msg) return;
        std::string payload(static_cast<const char*>(msg->payload), size_t(msg->payloadlen));
        listener_->onMqttMessage(msg->topic, payload.c_str());
    }

    static void onConnectCb(struct mosquitto*, void* obj, int rc) {
        static_cast<MosquittoTransport*>(obj)->handleConnect(rc);
    }
    static void onMessageCb(struct mosquitto*, void* obj, const struct mosquitto_message* msg) {
        static_cast<MosquittoTransport*>(obj)->handleMessage(msg);
    }
    static void onDisconnectCb(struct mosquitto*, void* obj, int) {
        auto* self = static_cast<MosquittoTransport*>(obj);
        self->connected_ = false;
        self->connecting_ = false;
        std::printf("[mqtt] Verbindung verloren\n");
    }

    struct mosquitto* mosq_ = nullptr;
    Listener* listener_ = nullptr;

    std::string host_, user_, pass_;
    int port_ = 1883;
    bool enabled_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    uint8_t attempts_ = 0;
    uint32_t nextAttemptMs_ = 0;
    uint32_t connectDeadlineMs_ = 0;
};

}  // namespace sim
