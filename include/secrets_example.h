// Nach include/secrets.h kopieren und ausfuellen.
// secrets.h steht in .gitignore und gehoert nicht ins Repo.
//
// Nur fuer die erste Inbetriebnahme. Sobald das Konfigurations-Schema und die
// Webapp stehen, kommen die Zugangsdaten aus dem LittleFS und werden ueber den
// Accesspoint eingerichtet (DESIGN 8.3, 9.1).
#pragma once

#define WIFI_SSID "MeinWLAN"
#define WIFI_PASS "geheim"

// MQTT-Broker. Leer lassen oder MQTT_HOST weglassen, um MQTT abzuschalten --
// dann meldet der Gesundheitskanal auch keine Stoerung.
#define MQTT_HOST "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
