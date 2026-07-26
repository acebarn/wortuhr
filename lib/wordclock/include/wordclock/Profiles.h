#pragma once

#include "wordclock/ClockRenderer.h"
#include "wordclock/Compositor.h"
#include "wordclock/Config.h"
#include "wordclock/DotRenderer.h"

namespace wordclock {

// Ableitung der Darstellung aus der Konfiguration.
//
// Getrennt von Config gehalten, damit Config nichts ueber Renderer wissen muss
// -- die Konfiguration beschreibt Werte, nicht ihre Wirkung.
//
// Im Nachtzustand werden Bewegung und Geisterwoerter abgeschaltet und Farbe
// sowie Helligkeit aus dem Nachtprofil genommen (DESIGN 6). Nacht ist kein
// Aus, sondern ein zweites Profil -- die Uhr bleibt ablesbar.

ClockStyle clockStyleFor(const Config& cfg, DisplayState state);
DotStyle dotStyleFor(const Config& cfg, DisplayState state);

// Globale Helligkeit fuer Modifiers.brightness.
uint8_t brightnessFor(const Config& cfg, DisplayState state);

}  // namespace wordclock
