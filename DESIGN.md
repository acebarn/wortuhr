# Wortuhr — Design

Neuentwicklung der Firmware für die selbstgebaute 11×10-Wortuhr. Ersetzt den Fork von
`techniccontroller/wordclock_esp8266`, der für ein 11×11-Panel ausgelegt war.

Dieses Dokument hält die Entscheidungen fest, **nicht** den Implementierungsstand.
Wenn Code und Dokument sich widersprechen, ist das ein Fehler im Code.

---

## 1. Hardware (unveränderlich)

| | |
|---|---|
| Controller | ESP8266, NodeMCU 1.0 (ESP-12E), 4 MB Flash |
| LEDs | 114 × WS2812, ein Strip, GRB, 800 kHz |
| Datenpin | D1 (GPIO5) |
| Panel | 11 Spalten × 10 Zeilen Buchstaben = 110 LEDs |
| Minutenpunkte | 4 LEDs in den Ecken der Frontplatte, Strip-Index 110–113 |
| Verlauf | Serpentine, zeilenweise, **erste LED oben rechts** |
| Bedienelemente | **keine** — kein Taster, kein Schalter, kein Sensor |

Zeile 0 läuft rechts→links, Zeile 1 links→rechts, usw. Daraus:

```
gerade Zeile:   index = y*11 + (10 - x)
ungerade Zeile: index = y*11 + x
```

Die Minutenpunkte sind die Fortsetzung des Strips nach dem letzten Buchstaben.
Sie werden in der Reihenfolge 113, 112, 111, 110 zugeschaltet (1 bis 4 Minuten).

Die vier Eckpunkte laufen **gegen den Uhrzeigersinn ab oben rechts** (am Gerät
ausgemessen). Die Rasterausrichtung wurde dabei bestätigt: erste LED oben rechts,
Zeile 0 oben.

```
Punkt 1 (112) ──── Punkt 0 (113)
      │                  │
Punkt 2 (111) ──── Punkt 3 (110)
```

---

## 2. Wortraster

Fest, weil in die Frontplatte geschnitten. Keine Umlaute.

```
     0 1 2 3 4 5 6 7 8 9 10
 0   E S K I S T A F U N F     ES, IST, FUNF
 1   Z E H N Z W A N Z I G     ZEHN, ZWANZIG
 2   D R E I V I E R T E L     DREIVIERTEL, VIERTEL
 3   V O R D I R S N A C H     VOR, NACH
 4   H A L B A E L F U N F     HALB, ELF, FUNF
 5   E I N S X A M Z W E I     EIN(S), ZWEI
 6   D R E I A U J V I E R     DREI, VIER
 7   S E C H S N L A C H T     SECHS, ACHT
 8   S I E B E N Z W O L F     SIEBEN, ZWOLF
 9   Z E H N E U N K U H R     ZEHN, NEUN, UHR
```

**Dialekt:** `DREIVIERTEL` für :45, nicht „VIERTEL VOR". Für :20 und :40 wird
auf die **volle Stunde** bezogen — `ZWANZIG NACH <laufende>` und
`ZWANZIG VOR <kommende>` — nicht auf die halbe.

Daraus folgt eine Besonderheit: Der Stundenbezug springt bei **:25**, nicht bei
:20. „Zwanzig nach sieben" meint sieben, „fünf vor halb acht" meint acht.

**Überlappungen im Raster** (bewusst, nie gleichzeitig aktiv):
`ELF`/`FUNF` teilen sich Index 51, `ZEHN`/`NEUN` teilen sich Index 102,
`VIERTEL` liegt vollständig in `DREIVIERTEL`.

Wörter werden **nicht** zur Laufzeit im Raster gesucht. Sie sind Konstanten mit
festem Offset; ein Test prüft die Konstanten gegen das Raster. Die Textsuche des
Altprojekts war die Quelle mehrerer Fehlerklassen.

---

## 3. Architektur

### 3.1 Schichten

```
lib/wordclock/   reines C++, kein Arduino.h, auf dem Host kompilierbar
                 Geometrie, Wortlogik, Compositor, Notify-Stapel,
                 Gesundheitsautomat, Zeitvertrauen, Config-Schema

src/             dünne Adapter: WiFi, MQTT, LittleFS, NeoPixelBus, Webserver
```

Alle Entscheidungslogik liegt im Kern und ist ohne Hardware testbar. Die Adapter
enthalten keine Fachlogik.

### 3.2 Rendering — Layer-Compositor

```
Basis (Uhr | Animation)
  + Overlay (Notify-Stapel)
  + Modifier (Profil, Helligkeit, Strombegrenzung)
  = targetBuffer
      ↓ lerp(faktor)
    currentBuffer → Strip
```

Die weiche Interpolation zwischen `current` und `target` wird aus dem Altprojekt
übernommen — sie ist der Grund, warum der Minutenwechsel gut aussieht.

Puffergröße: 114 × 3 Byte × 2 = 684 Byte. Unkritisch.

---

## 4. Statusanzeige auf den Eckpunkten

**Der Vertrag: Ruhe bedeutet gesund. Jede Bewegung bedeutet Störung.**

Die Uhrenfarbe ist frei konfigurierbar, also kann Farbe allein kein Signal tragen.
Der Rhythmus ist der Träger; Farbe und Anzahl präzisieren nur.

| Rhythmus | Bedeutung |
|---|---|
| ruhig | alles in Ordnung |
| langsam atmend | Warnung |
| schnell blinkend | kritisch |
| Lauflicht | wartet auf Benutzereingriff |

**Zweistufig nach Schweregrad:**

- **Warnung** — die Anzahl bleibt die Minute, nur die Farbe ändert sich.
  Die Uhrzeit ist korrekt, du verlierst keine Information.
  *Ausnahme:* Bei Minute 0, 5, 10 … leuchtet regulär kein einziger Punkt — eine
  Warnung wäre dort unsichtbar, die Uhr also ein Fünftel der Zeit warnblind.
  Deshalb zeigt eine Warnung mindestens einen Punkt. Der Rhythmus macht sie
  eindeutig; die Minutenangabe ist in diesem einen Fall um eins zu hoch.
- **Kritisch** — Anzahl *und* Farbe werden zum Fehlercode. Zulässig, weil in diesen
  Zuständen keine gültige Zeit existiert, die die Minuten anzeigen könnten.
  Die Punkte lügen also nie.

| Code | Zustand |
|---|---|
| 1 rot blinkend | kein WLAN |
| 2 rot blinkend | WLAN vorhanden, keine gültige Zeit |
| 3 rot blinkend | Dateisystem/Konfiguration defekt |
| 4 blau Lauflicht | AP-Modus, Einrichtung nötig |

Die Eckpunkte sind **ausschließlich** Gesundheitskanal. Benachrichtigungen dürfen
sie nicht verwenden.

---

## 5. Zeit

- NTP, POSIX-TZ-String `CET-1CEST,M3.5.0,M10.5.0/3` über `configTime()`.
  Keine eigene Sommerzeitmathematik.
- Zeit wird **nicht** von HomeAssistant bezogen — die Uhr muss allein funktionieren.

**Vertrauensschwellen**, hergeleitet aus dem Anzeigeschritt: Die Anzeige ändert sich
alle 5 Minuten, ein sichtbarer Fehler entsteht also erst bei ~150 s Abweichung.
Der ESP8266 driftet frei laufend in der Größenordnung Sekunden pro Tag.

| seit letztem Sync | Verhalten |
|---|---|
| nie synchronisiert | Wortfeld dunkel, 2 Punkte rot blinkend — zeigt lieber nichts als Erfundenes |
| < 3 Tage | normal, keine Meldung |
| 3–14 Tage | Uhr läuft normal, Punkte atmen langsam |
| > 14 Tage | Punkte blinken schnell, Anzeige kann abweichen |

---

## 6. Darstellungszustände

Drei Zustände, jeder mit eigenem Profil:

| | Tag | Nacht | Aus |
|---|---|---|---|
| Wortfeld | normal | gedimmt, warm | dunkel |
| Übergänge | konfiguriert | hart | — |
| Geisterwörter | konfiguriert | aus | — |
| Sekundenatmen | konfiguriert | aus | — |
| Eckpunkte bei Fehler | ja | ja | **ja, blinkend** |

Der Aus-Zustand existiert, weil der Raum gelegentlich als Schlafzimmer dient.
Kritische Fehler durchbrechen ihn trotzdem und blinken wie im Normalbetrieb.

> **Bekannte Kette:** Blinken im dunklen Raum + kein Bedienelement → jemand zieht den
> Stecker. Beim fünften Mal löst das den Werksreset aus (siehe 9.2). Der Zähler
> verfällt nach 10 s Laufzeit, versehentliches Auslösen ist daher unwahrscheinlich.

**Auslösung, gestaffelt:** HomeAssistant hat Vorrang → sonst eigenes Zeitfenster →
bei MQTT-Verlust automatischer Rückfall auf das Zeitfenster.

---

## 7. Darstellungs-Features

### 7.1 Typografie — drei Rollen, nicht sechs Schalter

**Rolle 1 — Übergang** (genau ein Wert aktiv, ~400 ms beim Minutenwechsel,
danach steht das Bild still):
`keiner` · `gestaffelt in Leserichtung` · `Fade von oben nach unten` · `herabfallende Buchstaben`

**Rolle 2 — statische Modifikatoren** (frei kombinierbar, bewegen sich nie):
Farbverlauf über die aktive Wortkette · Geisterwörter (Grundhelligkeit der
unbenutzten Rasterwörter)

**Rolle 3 — Dauerbewegung** (ein Schalter):
Sekundenatmen. Ist es aktiv, weicht der Notify-Stapel automatisch von `pulse` auf
`blink` aus, weil `pulse` sonst nicht mehr vom Grundrhythmus zu trennen wäre.

### 7.2 Animationen — Interpunktion, nicht Inhalt

Vollflächige Animationen verdecken zwangsläufig die Uhrzeit; 110 Pixel sind
gleichzeitig die Buchstaben. Sie sind daher an Anlässe gebunden:

- **Zur vollen Stunde** — 2–3 s, im Nacht- und Aus-Zustand unterdrückt
- **Von HA ausgelöst** — als Primitiv `anim` im Notify-Vertrag
- **Als Dauermodus** — gestartet aus einer HA-Szene

*Nicht* umgesetzt: Animationen bei Geräte-Zustandsübergängen.

**Der Dauermodus läuft, bis HA ihn beendet**, mit genau zwei Ausnahmen:
Abriss der MQTT-Verbindung und Beginn eines Nacht- oder Aus-Fensters.

### 7.3 Primitive

Ein gemeinsamer Satz speist Minutenübergang, Stundenschlag und Dauermodus —
dieselben Bausteine, andere Dauer und Deckung.

| Primitiv | Parameter |
|---|---|
| `wipe` | Richtung, Farbe, Tempo |
| `fall` | Dichte, Spurlänge, Tempo, Palette |
| `ripple` | Ursprung, Tempo, Palette |
| `wave` | Winkel, Tempo, Palette |
| `noise` | Skalierung, Tempo, Palette |
| `sparkle` | Dichte, Abklingzeit, Palette |
| `fire` | eigenes Wärmemodell — aus den anderen nicht darstellbar |
| `rainbow` | Ursprung, Tempo, Waberstärke — eigene Palette: das Spektrum |

`rainbow` ist das einzige Primitiv ohne Stützfarben. Zwei Farben können einen
Regenbogen nicht beschreiben; `from`/`to` bleiben dort wirkungslos. Es ist auch
kein `ripple` mit anderer Palette: der Abstand zum Ursprung wird von zwei
ungleich schnellen Wellen verbogen, bevor daraus ein Farbton wird — deshalb
wabern die Ringe, statt bloß zu pulsieren.

**Presets** (`matrix`, `nordlicht`, `silvester`, `sonnenaufgang`, `feuer`,
`welle`, `tropfen`, `plasma`, `regenbogen`) sind benannte Parameterbündel als
Tabelle in der Firmware, kein eigener Code.

> **Teilweise umgesetzt:** Das Design sagt, *ein* Primitivsatz speise alle drei
> Rollen. Stundenschlag und Dauermodus teilen sich den `Animator`. Der
> Minutenübergang arbeitet weiterhin eigenständig im `ClockRenderer` — er wirkt
> auf die Buchstaben der Wortkette, die Animationen auf die ganze Fläche. Das
> zusammenzuführen hieße, Primitiven eine Deckungsmaske zu geben; verschoben.

### 7.4 Geparkt

Digitaluhr. Auf 11×10 passen mit der 3×5-Ziffer vier Ziffern nur in zwei Reihen zu
je 5 Zeilen — es bleibt keine Trennzeile, Stunden- und Minutenziffern verschmelzen.
(Genau dieser Fehler steckte unbemerkt im Altprojekt.) Lösbar über Farbtrennung oder
eine 3×4-Ziffer. Niedrige Priorität.

Ersatzlos gestrichen: Tetris, Snake, Pong.

---

## 8. Backend

### 8.1 Hoheit

**Die Uhr ist alleinige Wahrheit.** Die Konfiguration liegt im Flash. HA und Webapp
senden Anfragen; die Uhr entscheidet, speichert und publiziert danach ihren Zustand.

- Command-Topics: **nie** retained (sonst Zombie-Befehle nach jedem Reboot)
- State-Topics: **immer** retained
- alle HA-Entitäten: `optimistic: false`
- LWT als Availability-Topic

Ist die Uhr offline, geht eine Änderung in HA verloren. Das ist beabsichtigt und
über LWT sofort sichtbar.

### 8.2 HomeAssistant

Einzelne Entitäten per MQTT-Discovery, weil HA auch Konfigurationsoberfläche ist —
nicht nur Fernbedienung. Diagnose-Entitäten tragen `entity_category: diagnostic`.

### 8.3 Config-Schema — eine Quelle

Es gibt **eine** Beschreibung aller Einstellungen. Aus ihr entstehen:

```
ConfigSchema[]
   ├── Persistenz        LittleFS, JSON
   ├── HA-Discovery      Entitäten + Command/State-Topics
   ├── Validierung       Grenzen, Typen, Vorgaben
   └── /api/schema       die Webapp baut ihr Formular daraus selbst
```

Eine neue Einstellung ist eine Zeile im Schema. Divergenz zwischen HA und Webapp ist
dadurch strukturell unmöglich, nicht eine Frage der Disziplin. Das Gerät liefert nur
JSON, kein HTML — die Webapp ist eine einzelne statische Datei.

### 8.4 Notify-Vertrag

Die Firmware kennt **keine Ereignisse**, nur Darstellungsprimitive. Klingel,
Waschmaschine und Mülltonne existieren ausschließlich als HA-Automationen.

```jsonc
// wortuhr/notify — nicht retained
{
  "id":    "fenster",     // Kanalname, zugleich Schlüssel zum Löschen
  "prio":  20,
  "style": "tint",        // tint | pulse | blink | glyph | word | anim
  "color": [0, 120, 255],
  "ttl":   30             // Sekunden; 0 = Dauerzustand
}
```

**Die sechs Primitive:**

| Style | Wirkung |
|---|---|
| `tint` | färbt das Wortfeld um, ruhig |
| `pulse` | färbt um **und** atmet |
| `blink` | färbt um **und** blinkt |
| `glyph` | 11×10-Bitmap über dem Wortfeld, nur gesetzte Bits decken ab |
| `word` | hebt ein Rasterwort hervor |
| `anim` | ersetzt die Basis-Ebene durch eine Animation |

Umfärben **ersetzt** den Farbton und behält die Helligkeit je Pixel — es ist
nicht multiplikativ. Multiplikation kann nie aufhellen: ein blauer `tint` auf
der bernsteinfarbenen Uhr ergäbe ein dunkles Petrol statt Blau, weil Bernstein
kaum Blauanteil hat. Verlauf, Übergang und Geisterwörter bleiben dabei erhalten.

Ist das Sekundenatmen aktiv, weicht `pulse` selbsttätig auf `blink` aus — sonst
wäre es vom Grundrhythmus nicht mehr zu trennen.

**Prioritätsstapel:** Dauerzustände liegen unten, kurze Ereignisse legen sich darüber
und fallen nach Ablauf ab — der Dauerzustand ist danach automatisch wieder sichtbar.
Sichtbar ist immer nur der oberste Eintrag; bei gleicher Priorität der zuletzt
eingetroffene.

**Selbstheilung:** `ttl` ist Pflicht außer bei ausdrücklichem `ttl: 0`. Auch
`ttl: 0`-Kanäle verfallen, wenn MQTT länger als 10 Minuten weg ist. Sonst färbt ein
verlorenes `clear`-Telegramm die Uhr wochenlang.

Benachrichtigungen erreichen die Eckpunkte nicht.

---

## 9. Netz und Zugang

### 9.1 WLAN — nie blockierend

`setup()` erreicht **immer** `loop()`. Verbindungsaufbau läuft als Zustandsautomat
mit wachsendem Wiederholungsabstand:

```
Versuch 1–3    alle 10 s
Versuch 4–6    alle 30 s
ab Versuch 7   alle 2 min  +  AP geht zusätzlich auf
                              STA-Versuch läuft weiter
```

Der AP ersetzt den Verbindungsversuch nicht, er kommt dazu. Kommt der Router zurück,
fängt die Uhr sich selbst wieder ein.

Das Altprojekt rief `wifiManager.autoConnect()` blockierend in `setup()` — ohne WLAN
beim Einschalten wurde `loop()` nie erreicht und die Uhr zeigte gar nichts mehr.

### 9.2 Notzugang ohne Bedienelement

- **Normalweg:** mDNS, `wortuhr.local` — keine IP-Adresse zu merken, unabhängig von DHCP
- **Letzte Instanz:** fünf kurz hintereinander abgebrochene Startvorgänge lösen
  Werksreset und AP aus. Zähler im Flash, Rücksetzung nach 10 s Laufzeit.
  Die Eckpunkte zeigen den Zählerstand, das Auslösen ist also sichtbar und abbrechbar.

Die IP-Adresse wird **nicht** dauerhaft auf dem Raster angezeigt.

---

## 10. Werkzeuge

| | Wahl | Begründung |
|---|---|---|
| Build | PlatformIO, Framework Arduino | reproduzierbar, Libs versioniert, Host-Tests |
| LEDs | NeoPixelBus | DMA/UART auf ESP8266, sperrt keine Interrupts |
| MQTT | PubSubClient | klein, synchron, bewährt |
| OTA | `espota` nach dem ersten USB-Flash | |
| Tests | `env:native`, Unity | Kern ohne Hardware prüfbar |
| Vorschau | `env:preview` | Panel im Terminal, ohne zu flashen |
| Simulator | `env:sim` | ganze Anwendung auf dem Rechner, echter Broker |

Die Vorschau benutzt die echte Kette `ClockRenderer → Compositor`, keine
Nachbildung — sonst würde sie etwas zeigen, das die Uhr nicht tut.

```
pio run -e preview && .pio/build/preview/program --help
```

### Simulator

Firmware und Simulator führen **denselben** Code aus: `wordclock::App` ist
portabel, unterschiedlich sind nur die Adapter hinter `Ports`. Ein Simulator,
der die Schleife nachbaut, würde mit der Zeit etwas anderes zeigen als das
Gerät tut — und genau das soll er ausschließen.

```
brew services start mosquitto
pio run -e sim && .pio/build/sim/program --help

mosquitto_sub -v -t 'homeassistant/#' -t 'wortuhr/#'
mosquitto_pub -t wortuhr/set/brightness -m 200
```

Zwei getrennte Zeitachsen: `millis()` läuft in Echtzeit, damit Übergänge
beurteilbar bleiben; die Wanduhr läuft beschleunigt, damit ein Tag-Nacht-Wechsel
nicht acht Stunden dauert.

**Logging:** HA-Diagnose-Entitäten als Dauerkanal, RAM-Ringpuffer über `/api/log`
für Details, Serial nur bei Entwicklung. Das UDP-Multicast-Logging entfällt.

**Benennung:** durchgängig `wortuhr` — mDNS-Name, MQTT-Prefix, HA-Gerätename.

---

## 10a. Speicherlage (Stand: Webapp fertig)

| | RAM | Flash |
|---|---|---|
| vor Webserver | 43,2 % | 33,9 % |
| mit Webserver + mDNS | **56,0 %** | 39,7 % |

~36 kB freier Heap. Der Sprung kommt von `ESP8266WebServer` und mDNS. Für den
Betrieb reicht das, aber es ist der Posten, den weitere Features zuerst treffen.

---

## 11. Offene Punkte

- Digitaluhr: 3×4-Ziffer oder Farbtrennung
- `glyph`-Primitiv: Format und Kodierung der 11×10-Bitmap
- Webapp und AP sind noch **nie am Gerät gelaufen** — nur im Simulator geprüft
