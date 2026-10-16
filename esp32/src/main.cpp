// PrioSense — ESP32-variant van de TETRA/C2000-activiteitsmelder.
//
// Smart Panlee SC01 Plus (ESP32-S3 + 3,5"-scherm) met één of twee Ai-Thinker
// Ra-02's (SX1278) als RSSI-meter. Meet per 25 kHz-kanaal in 380,9–384,1 MHz (zelfde venster als de Pi) hoe
// sterk het signaal is en geeft een melding bij activiteit vlakbij.
//
// WETTELIJK: alleen ontvangen en signaalsterkte meten. Er wordt niets
// gedecodeerd of opgeslagen en de radio's zenden nooit.
//
// Taakverdeling: core 0 scant (radio's + detectie), core 1 doet scherm en touch.
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include "beeper.h"
#include "config.h"
#include "detector.h"
#include "sx1278_rssi.h"
#include "ui.h"

namespace {
Display lcd;
SPIClass radioSpi(FSPI);
Sx1278Rssi radioA(radioSpi, PIN_RADIO_NSS_A);
Sx1278Rssi radioB(radioSpi, PIN_RADIO_NSS_B);
Sx1278Rssi* radios[2];
int nRadios = 0;

Detector detector;
SemaphoreHandle_t lock;                  // beschermt detector + snapshot
DetectorSnapshot snap;
volatile uint32_t sweepCount = 0;
volatile float sweepMs = 0;

Preferences prefs;
ui::State state = {0, false, false};

uint32_t channelHz(int i) { return BAND_START_HZ + (uint32_t)i * CHANNEL_HZ; }

// Kanalen verdelen: met twee radio's scant elke radio de helft → dubbel zo snel rond.
// segStart/segLen = welk stuk van de band bij welke radio hoort (voor de CFAR),
// order = in welke volgorde die radio z'n kanalen afloopt.
int segStart[2], segLen[2];
int order[2][N_CHANNELS];

void planScan() {
    if (nRadios == 2) {
        // A: van 382,475 naar beneden tot 380,900; B: van 382,500 naar boven.
        segStart[0] = 0;          segLen[0] = CENTER_IDX;
        segStart[1] = CENTER_IDX; segLen[1] = N_CHANNELS - CENTER_IDX;
        for (int k = 0; k < segLen[0]; ++k) order[0][k] = CENTER_IDX - 1 - k;
        for (int k = 0; k < segLen[1]; ++k) order[1][k] = CENTER_IDX + k;
    } else if (nRadios == 1) {
        // Eén radio: vanuit het midden om en om naar beneden en naar boven.
        segStart[0] = 0; segLen[0] = N_CHANNELS;
        int k = 0;
        order[0][k++] = CENTER_IDX;
        for (int d = 1; k < N_CHANNELS; ++d) {
            if (CENTER_IDX - d >= 0) order[0][k++] = CENTER_IDX - d;
            if (CENTER_IDX + d < N_CHANNELS && k < N_CHANNELS) order[0][k++] = CENTER_IDX + d;
        }
    }
}

void scanTask(void*) {
    static float rssi[N_CHANNELS];
    uint32_t statT0 = millis(), statSweeps = 0;
    for (;;) {
        uint32_t t0 = micros();
        int steps = 0;
        for (int s = 0; s < nRadios; ++s) steps = max(steps, segLen[s]);
        for (int k = 0; k < steps; ++k) {
            // Alle radio's tegelijk afstemmen, één keer wachten, dan uitlezen.
            for (int s = 0; s < nRadios; ++s)
                if (k < segLen[s]) radios[s]->tune(channelHz(order[s][k]));
            delayMicroseconds(DWELL_US);
            for (int s = 0; s < nRadios; ++s)
                if (k < segLen[s]) rssi[order[s][k]] = radios[s]->readRssiDbm();
        }
        float now = millis() / 1000.0f;
        sweepMs = (micros() - t0) / 1000.0f;

        xSemaphoreTake(lock, portMAX_DELAY);
        const DriveMode& m = MODES[state.mode];
        detector.setThresholds(m.soft, m.hard);
        detector.process(rssi, segStart, segLen, nRadios, now);
        detector.snapshot(snap);
        int alarm = snap.alarm;
        float db = snap.alarmDb;
        xSemaphoreGive(lock);

        beeper::update(state.muted ? 0 : alarm, db, m.soft);
        ++sweepCount;
        ++statSweeps;

        // Elke paar seconden een regel in de seriële log (zoals "dekking" op de Pi).
        if (millis() - statT0 >= STAT_INTERVAL_S * 1000) {
            float el = (millis() - statT0) / 1000.0f;
            int top = -1;
            for (int i = 0; i < N_CHANNELS; ++i)
                if (top < 0 || rssi[i] > rssi[top]) top = i;
            Serial.printf("[scan] %d radio('s), %.1f rondes/s (%.1f ms/ronde), ruisvloer %.1f dBm, "
                          "sterkste %.4f MHz %.1f dBm, alarm %d\n",
                          nRadios, statSweeps / el, sweepMs, detector.floorDbm(),
                          channelHz(top) / 1e6, rssi[top], alarm);
            statT0 = millis();
            statSweeps = 0;
        }
        vTaskDelay(1);   // watchdog tevreden houden
    }
}

void saveSettings() {
    prefs.putUChar("mode", state.mode);
    prefs.putBool("muted", state.muted);
}
}

void setup() {
    Serial.begin(115200);
    lock = xSemaphoreCreateMutex();
    prefs.begin("priosense", false);
    state.mode = min<int>(prefs.getUChar("mode", 0), N_MODES - 1);
    state.muted = prefs.getBool("muted", false);

    ui::begin(lcd);
    ui::splash(0.1f, "Starting...");
    beeper::begin();

    // Gezamenlijke reset van beide radio's.
    pinMode(PIN_RADIO_RESET, OUTPUT);
    digitalWrite(PIN_RADIO_RESET, LOW);
    delay(2);
    digitalWrite(PIN_RADIO_RESET, HIGH);
    delay(10);
    radioSpi.begin(PIN_RADIO_SCK, PIN_RADIO_MISO, PIN_RADIO_MOSI, -1);
    // Beide NSS hoog vóór de eerste SPI-transactie, anders praten ze door elkaar.
    pinMode(PIN_RADIO_NSS_A, OUTPUT); digitalWrite(PIN_RADIO_NSS_A, HIGH);
    pinMode(PIN_RADIO_NSS_B, OUTPUT); digitalWrite(PIN_RADIO_NSS_B, HIGH);

    const uint32_t center = CENTER_HZ;
    ui::splash(0.35f, "Radio A...");
    bool okA = radioA.begin(center);
    ui::splash(0.6f, "Radio B...");
    bool okB = radioB.begin(center);
    Serial.printf("[radio] A: %s (versie 0x%02X), B: %s (versie 0x%02X)\n",
                  okA ? "ok" : "NIET GEVONDEN", radioA.version(),
                  okB ? "ok" : "NIET GEVONDEN", radioB.version());
    if (okA) radios[nRadios++] = &radioA;
    if (okB) radios[nRadios++] = &radioB;

    planScan();

    if (nRadios == 0) {
        ui::splash(1.0f, "No radio found - check wiring");
        delay(4000);
    } else {
        ui::splash(1.0f, nRadios == 2 ? "2 radios ready" : "1 radio ready");
        delay(800);
        xTaskCreatePinnedToCore(scanTask, "scan", 8192, nullptr, 3, nullptr, 0);
    }
    ui::drawMain(state);
}

void loop() {
    static uint32_t lastSweep = 0, lastSweepMs = 0, phaseMs = 0;
    static bool phase = false;
    uint32_t now = millis();

    switch (ui::pollTouch()) {
        case ui::Touch::City:      if (state.mode != 0) { state.mode = 0; saveSettings(); } break;
        case ui::Touch::Highway:   if (state.mode != 1) { state.mode = 1; saveSettings(); } break;
        case ui::Touch::SoundTap:  state.muted = !state.muted; saveSettings(); break;
        case ui::Touch::SoundHold: beeper::test(); break;
        case ui::Touch::None:      break;
    }

    // Hartslag: knippert groen zolang er rondes binnenkomen, rood als dat ~2 s stokt.
    if (sweepCount != lastSweep) { lastSweep = sweepCount; lastSweepMs = now; }
    state.scanning = nRadios > 0 && now - lastSweepMs < 2000;
    if (now - phaseMs >= 500) { phaseMs = now; phase = !phase; }

    static DetectorSnapshot local;
    xSemaphoreTake(lock, portMAX_DELAY);
    local = snap;
    xSemaphoreGive(lock);
    ui::update(state, local, phase);

    delay(30);   // ~30 beelden per seconde is ruim genoeg
}
