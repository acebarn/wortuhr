# Wortuhr

Firmware für eine selbstgebaute Wortuhr: ESP8266, 114 WS2812-LEDs, 11 × 10
Buchstaben plus vier Minutenpunkte in den Ecken.

Die Uhr wird über **HomeAssistant** bedient und hat eine **Fallback-Webapp**
mit eigenem Accesspoint für den Fall, dass nichts davon erreichbar ist.
Entwurfsentscheidungen und ihre Begründungen stehen in [DESIGN.md](DESIGN.md).

---

## Die vier Eckpunkte

Die Uhr hat **kein Bedienelement** — keinen Taster, keinen Schalter. Die vier
Punkte in den Ecken sind deshalb ihre einzige Möglichkeit, von sich aus etwas
mitzuteilen. Sie sind kein Schmuck, sondern ein Anzeigeinstrument.

### Die Grundregel

> **Ruhe heißt gesund. Jede Bewegung heißt Störung.**

Die Uhrenfarbe ist frei einstellbar. Eine rote Uhr würde einen roten Fehlerpunkt
verschlucken — **Farbe allein kann das Signal also nicht tragen.** Es steckt im
Rhythmus. Farbe und Anzahl präzisieren nur.

Du musst dir keine Farbtabelle merken. Es genügt: *bewegt sich etwas, hinschauen.*

### Anordnung

Am Gerät ausgemessen. Die Punkte füllen **gegen den Uhrzeigersinn ab oben
rechts** auf — das ist die Reihenfolge des LED-Strips.

```
        ┌─────────────────────────────┐
   ②    │  E  S  K  I  S  T  A  F  U  N  F   │    ①      ① = 1. Minute
        │  Z  E  H  N  Z  W  A  N  Z  I  G   │
        │  D  R  E  I  V  I  E  R  T  E  L   │
        │  V  O  R  D  I  R  S  N  A  C  H   │
        │  H  A  L  B  A  E  L  F  U  N  F   │
        │  E  I  N  S  X  A  M  Z  W  E  I   │
        │  D  R  E  I  A  U  J  V  I  E  R   │
        │  S  E  C  H  S  N  L  A  C  H  T   │
        │  S  I  E  B  E  N  Z  W  O  L  F   │
   ③    │  Z  E  H  N  E  U  N  K  U  H  R   │    ④
        └─────────────────────────────┘
```

### Gesund — ruhig

Die Anzahl ist die **Minute innerhalb des Fünf-Minuten-Schritts**, in Uhrenfarbe,
völlig unbewegt.

```
07:45   ○ ○     kein Punkt      07:47   ● ○     zwei Punkte
        ○ ○                             ● ○

07:46   ● ○     ein Punkt       07:48   ● ●     drei Punkte
        ○ ○                             ● ○
```

Das Wortfeld zeigt in allen vier Fällen dasselbe: **ES IST DREIVIERTEL ACHT**.
Die Punkte tragen die Minuten, die zwischen den Sprüngen liegen.

### Warnung — langsames Atmen

Die Uhr geht richtig, aber etwas läuft nicht rund.

```
        ● ○     Anzahl bleibt die Minute
        ○ ○     Farbe wird Amber
                Helligkeit atmet über 3 Sekunden, wird nie ganz dunkel
```

| Auslöser | Bedeutung |
|---|---|
| HomeAssistant nicht erreichbar | Uhr läuft, nur die Fernbedienung fehlt |
| Zeitsync älter als 3 Tage | Anzeige noch korrekt, aber Netz prüfen |

**Du verlierst keine Information** — die Minutenanzeige bleibt erhalten.

> Eine Ausnahme: Bei Minute 0, 5, 10 … leuchtet regulär *kein* Punkt. Dort zeigt
> eine Warnung mindestens einen, sonst wäre sie ein Fünftel der Zeit unsichtbar.
> Die Minutenangabe ist in diesem einen Fall um eins zu hoch.

Dass das Atmen nie ganz erlischt, ist Absicht: Sonst wäre es bei flüchtigem Blick
vom Blinken nicht zu unterscheiden — und die Trennung Warnung/kritisch wäre weg.

### Kritisch — schnelles Blinken

Anzahl **und** Farbe werden zum Fehlercode. Das ist zulässig, weil in diesen
Zuständen ohnehin keine verlässliche Zeit existiert, deren Minuten man anzeigen
könnte. **Die Punkte lügen nie.**

```
Code 1   ● ○   rot, blinkt      kein WLAN
         ○ ○

Code 2   ● ●   rot, blinkt      WLAN da, aber keine gültige Zeit
         ○ ○                    → Wortfeld bleibt dunkel, wenn die Uhr
                                  noch nie gestellt war

Code 3   ● ●   rot, blinkt      Dateisystem oder Konfiguration defekt
         ● ○                    → Uhr läuft mit Vorgaben weiter

Code 4   ◐ ◑   blau, Lauflicht  AP-Modus, Einrichtung nötig
         ◑ ◐                    → verbinde dich mit „Wortuhr-Setup"
```

Das **Lauflicht** ist der einzige Rhythmus, der nicht „etwas ist kaputt" heißt,
sondern **„ich warte auf dich"**. Ein Punkt wandert im Kreis, die übrigen glimmen
schwach mit, damit die Vier als Gruppe erkennbar bleibt.

### Nachts und im Aus-Zustand

Im **Nachtprofil** verhalten sich die Punkte wie am Tag, nur gedimmt.

Im **Aus-Zustand** — für Nächte, in denen jemand in dem Raum schläft — bleibt
alles dunkel. **Außer bei kritischen Fehlern:** die brechen durch und blinken wie
im Normalbetrieb. Ein Ausfall soll nicht bis zum Morgen verborgen bleiben.

### Was die Punkte niemals tun

Benachrichtigungen aus HomeAssistant — Klingel, Fenster, Waschmaschine —
erreichen die Eckpunkte **nie**. Sie gehören ausschließlich dem
Gesundheitskanal. Andernfalls wäre nach kurzer Zeit unklar, ob eine Bewegung
Störung oder Paketbote bedeutet, und der Kanal wäre wertlos.

Das ist im Code verankert und durch Tests festgehalten, nicht bloß eine
Verabredung.

---

## Wie die Uhr spricht

Die Anzeige springt in Fünf-Minuten-Schritten. Zwei Eigenheiten des Rasters:

**`DREIVIERTEL` statt „viertel vor"** — süddeutsch/fränkisch, so in die
Frontplatte geschnitten.

**Zwanzig bezieht sich auf die volle Stunde**, nicht auf die halbe:

| Uhrzeit | Anzeige |
|---|---|
| 19:15 | ES IST VIERTEL NACH SIEBEN |
| 19:20 | ES IST **ZWANZIG NACH SIEBEN** |
| 19:25 | ES IST FUNF VOR HALB ACHT |
| 19:30 | ES IST HALB ACHT |
| 19:35 | ES IST FUNF NACH HALB ACHT |
| 19:40 | ES IST **ZWANZIG VOR ACHT** |
| 19:45 | ES IST DREIVIERTEL ACHT |

Der Stundenbezug springt dadurch bei **:25**, nicht bei :20 — „zwanzig nach
sieben" meint sieben, „fünf vor halb acht" meint acht.

Ohne Umlaute: FUNF, ZWOLF. Und `EIN UHR`, aber `FUNF NACH EINS`.

---

## Entwickeln ohne Gerät

Der Simulator führt **dieselbe Firmware** aus wie die Uhr — `wordclock::App` ist
portabel, unterschiedlich sind nur die Adapter. Eine Nachbildung würde
über kurz oder lang etwas anderes zeigen als das Gerät tut.

```bash
brew services start mosquitto      # echter Broker, keine Attrappe
pio run -e sim
.pio/build/sim/program --speed 60
```

| | |
|---|---|
| http://localhost:8080/panel | Wortuhr-Ansicht wie an der Wand |
| http://localhost:8080/ | Fallback-Konfigurationsseite |

Zustände vortäuschen: `--no-wifi`, `--ap`, `--no-sync`, `--sync-age 400000`.
Die Wanduhr läuft beschleunigt, die Bewegung bleibt in Echtzeit — sonst wären
Übergänge und Blinken nicht mehr zu beurteilen.

Bedient wird der Simulator wie die Uhr, über MQTT:

```bash
mosquitto_pub -t wortuhr/set/brightness -m 200
mosquitto_pub -t wortuhr/notify \
  -m '{"id":"klingel","prio":90,"style":"blink","color":[0,120,255],"ttl":8}'

mosquitto_sub -v -t 'homeassistant/#' -t 'wortuhr/#'
```

---

## Bauen und flashen

```bash
pio test -e native            # Kern ohne Hardware, ~130 Testfälle
pio run -e wortuhr -t upload  # Firmware
pio run -e preview            # nur das Panel im Terminal
```

Vor dem ersten Flashen `include/secrets_example.h` nach `include/secrets.h`
kopieren und ausfüllen. Das ist nur die **Erstbefüllung** — was einmal über die
Webapp gesetzt wurde, überschreibt ein Neuflashen nicht.

Der verbaute CH340 verträgt kein 921600 Baud; `platformio.ini` steht deshalb auf
115200.

---

## Wenn nichts mehr geht

Die Uhr hat kein Bedienelement. Es gibt zwei Wege zurück:

1. **`http://wortuhr.local`** — per mDNS, unabhängig von DHCP und HomeAssistant.
2. **Fünfmal kurz hintereinander den Stecker ziehen** — löst Werksreset und
   Accesspoint aus. Der Zähler verfällt nach 10 Sekunden Laufzeit, versehentlich
   passiert das also nicht. Die Eckpunkte zeigen den Stand mit.

Danach öffnet die Uhr das Netz **„Wortuhr-Setup"** und zeigt Code 4.
