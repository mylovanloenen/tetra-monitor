// Scherm: splash (zelfde als op de Pi) en het hoofdscherm — City/Highway,
// hartslag-lampje, Sound, en daaronder tien segmenten.
#pragma once
#include "display.h"
#include "detector.h"

namespace ui {
void begin(Display& lcd);
void splash(float pct, const char* text);   // pct < 0 = zonder voortgangsbalk

struct State {
    int  mode;          // 0 = City, 1 = Highway
    bool muted;
    bool scanning;      // hartslag groen (knippert) of rood (vastgelopen/geen radio)
};
void drawMain(const State& st);             // volledig opnieuw tekenen
// Elk frame: alleen wat veranderd is wordt opnieuw getekend.
void update(const State& st, const DetectorSnapshot& snap, bool heartbeatPhase);

enum class Touch { None, City, Highway, SoundTap, SoundHold };
Touch pollTouch();                          // vertaalt aanrakingen naar knoppen
}
