// PrioSense-simulator: draait de échte scherm- en detectiecode (src/ui.cpp,
// src/detector.cpp) in een venster op de Mac. De radio's zijn vervangen door
// nagemaakte RSSI-waarden volgens een vast scenario. Muis = touch.
//
//   ./build.sh && ./priosense_sim            → interactief
//   ./priosense_sim --shots out/             → maakt PNG's van elke fase en stopt
#include <Arduino.h>
#include <cstdio>
#include <random>
#include <string>
#include "../src/config.h"
#include "../src/detector.h"
#include "../src/ui.h"

namespace {
Display lcd(320, 480);   // staand paneel, net als de SC01 Plus; rotatie maakt 'm liggend
Detector detector;
DetectorSnapshot snap;
ui::State state = {0, false, true};
std::mt19937 rng(7);
std::normal_distribution<float> noise(0, 1.5f);
std::string shotDir;

// Scenario van 40 s, blijft herhalen. dB = boven de ruisvloer (−120 dBm).
struct Phase { float until; const char* name; };
const Phase PHASES[] = {
    {6,  "stil (ruisvloer opbouwen)"},
    {10, "zwak signaal 30 dB op 382,150 MHz"},
    {16, "portofoon nadert: 30 -> 55 dB op 383,700 MHz"},
    {24, "weer stil: balk blijft 4 s staan, zakt dan"},
    {28, "zender pal naast je (oversturing)"},
    {40, "stil"},
};
int phaseOf(float t) { int i = 0; while (t >= PHASES[i].until) ++i; return i; }

void makeSweep(float t, float* rssi) {
    for (int i = 0; i < N_CHANNELS; ++i) rssi[i] = -120 + noise(rng);
    switch (phaseOf(t)) {
        case 1: rssi[50] = -90; break;                                  // 382,150 MHz
        case 2: rssi[112] = -90 + 25 * (t - 10) / 6; break;             // 383,700 MHz
        case 4: for (int i = 0; i < N_CHANNELS; ++i) rssi[i] = -30 + noise(rng); break;
        default: break;
    }
}

void shot(const char* name) {
    size_t len = 0;
    void* png = lcd.createPng(&len, 0, 0, lcd.width(), lcd.height());
    std::string path = shotDir + "/" + name + ".png";
    if (FILE* f = fopen(path.c_str(), "wb")) { fwrite(png, 1, len, f); fclose(f); }
    free(png);
    printf("  -> %s\n", path.c_str());
}
}

void setup() {
    ui::begin(lcd);
    for (float p = 0; p <= 1.0f; p += 0.25f) {
        ui::splash(p, p < 1 ? "Radio A... Radio B..." : "2 radios ready");
        if (!shotDir.empty() && p == 0.5f) shot("0_splash");
        lgfx::delay(250);
    }
    ui::drawMain(state);
}

void loop() {
    static float rssi[N_CHANNELS];
    static int lastPhase = -1, lastAlarm = -1;
    static uint32_t start = millis();
    float now = millis() / 1000.0f;
    float t = fmodf((millis() - start) / 1000.0f, 40.0f);

    switch (ui::pollTouch()) {
        case ui::Touch::City:      state.mode = 0; break;
        case ui::Touch::Highway:   state.mode = 1; break;
        case ui::Touch::SoundTap:  state.muted = !state.muted; break;
        case ui::Touch::SoundHold: printf("[sim] geigerteller-test\n"); break;
        case ui::Touch::None:      break;
    }

    makeSweep(t, rssi);
    const int segStart[2] = {0, N_CHANNELS / 2}, segLen[2] = {N_CHANNELS / 2, N_CHANNELS / 2};
    const DriveMode& m = MODES[state.mode];
    detector.setThresholds(m.soft, m.hard);
    detector.process(rssi, segStart, segLen, 2, now);
    detector.snapshot(snap);
    ui::update(state, snap, (millis() / 500) % 2);

    int ph = phaseOf(t);
    if (ph != lastPhase) { printf("[sim] %4.1f s  %s (%s)\n", t, PHASES[ph].name, m.name); lastPhase = ph; }
    if (snap.alarm != lastAlarm) {
        printf("[sim]          alarm %s %s\n", snap.alarm == 2 ? "ROOD" : snap.alarm == 1 ? "ORANJE" : "uit",
               snap.overload ? "(oversturing)" : "");
        lastAlarm = snap.alarm;
    }

    if (!shotDir.empty()) {
        // Vaste momenten vastleggen, daarna stoppen.
        struct S { float t; const char* name; int mode; bool done; };
        static S shots[] = {
            {5.5f,  "1_stil", 0, false},
            {9.5f,  "2_zwak_city", 0, false},
            {13.0f, "3_nadert_city", 0, false},
            {15.8f, "4_dichtbij_city", 0, false},
            {19.0f, "5_hold", 0, false},
            {22.5f, "6_release", 0, false},
            {27.0f, "7_oversturing", 0, false},
            {37.0f, "8_stil_muted", 0, false},
        };
        if (t > 30 && !state.muted) { state.muted = true; }
        for (auto& s : shots)
            if (!s.done && t >= s.t) { shot(s.name); s.done = true; }
        if (shots[7].done) { std::exit(0); }
    }
    lgfx::delay(30);
}

int user_func(bool* running) {
    setup();
    do { loop(); } while (*running);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--shots") shotDir = argv[2];
    return lgfx::Panel_sdl::main(user_func);
}
