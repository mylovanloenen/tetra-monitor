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
ui::State state = {0, DEFAULT_VOLUME, false};
uint32_t testMuteAtMs = 0;   // na een test bij "uit" het volume weer op 0

uint32_t channelHz(int i) { return BAND_START_HZ + (uint32_t)i * CHANNEL_HZ; }
uint32_t frfOf[N_CHANNELS];   // vooraf berekend: geen double-rekenwerk tijdens het scannen

// Kanalen op de eigen stoorpiek van de Ra-02 (alleen bij normaal scannen).
bool spurMask[N_CHANNELS];
void buildSpurMask() {
    for (int i = 0; i < N_CHANNELS; ++i) {
        spurMask[i] = false;
        if (TEST_433 || TEST_DOWNLINK) continue;
        if (channelHz(i) < EDGE_LOW_MASK_HZ) spurMask[i] = true;
        for (uint32_t f : SPUR_HZ) {
            uint32_t c = channelHz(i);
            uint32_t d = c > f ? c - f : f - c;
            if (d <= SPUR_HALF_HZ) spurMask[i] = true;
        }
    }
}

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
        // Eén radio: in een rechte lijn door de band (stappen van 25 kHz). Om en om
        // vanuit het midden gaf sprongen tot ~3 MHz; daarna was de radio nog niet
        // op frequentie en "zag" hij sterke zenders op gespiegelde kanalen.
        segStart[0] = 0; segLen[0] = N_CHANNELS;
        for (int k = 0; k < N_CHANNELS; ++k) order[0][k] = k;
    }
}

// Heen-en-weer: de ene ronde de kanalen op volgorde, de volgende achterstevoren.
// Zo is er nooit een grote frequentiesprong, ook niet aan het eind van een ronde.
bool sweepReverse = false;
// Wat elke radio echt afloopt: zijn kanalen zonder de stoorpiek-kanalen.
int scanOrder[2][N_CHANNELS], scanLen[2];
void buildScanLists() {
    for (int s = 0; s < nRadios; ++s) {
        scanLen[s] = 0;
        for (int k = 0; k < segLen[s]; ++k)
            if (!spurMask[order[s][k]]) scanOrder[s][scanLen[s]++] = order[s][k];
    }
}
int pp(int s, int k) { return scanOrder[s][sweepReverse ? scanLen[s] - 1 - k : k]; }

void scanTask(void*) {
    static float rssi[N_CHANNELS];
    int gainStep = 1;                      // LNA-stand (1 = maximaal)
    uint32_t veryStrong = 0;               // metingen boven VERY_STRONG_DBM (voor de log)
    for (int i = 0; i < N_CHANNELS; ++i) rssi[i] = -130.0f;
    float lastGainStep = 0, lastLoud = 0, lastStrong = -100;
    uint32_t statT0 = millis(), statSweeps = 0;
    for (;;) {
        uint32_t t0 = micros();
        int steps = 0;
        for (int s = 0; s < nRadios; ++s) steps = max(steps, scanLen[s]);
        // De radio-bus is alleen van deze taak: één transactie voor de hele ronde.
        if (nRadios > 0) radios[0]->beginBurst();
        for (int k = 0; k < steps; ++k) {
            // Alle radio's tegelijk afstemmen, één keer wachten, dan uitlezen.
            for (int s = 0; s < nRadios; ++s)
                if (k < scanLen[s]) radios[s]->tuneFast(frfOf[pp(s, k)]);
            delayMicroseconds(DWELL_US);   // stap van 25 kHz: PLL staat snel, daarna RSSI-meting
            for (int s = 0; s < nRadios; ++s)
                if (k < scanLen[s]) {
                    float v = radios[s]->readRssiFast();
                    rssi[pp(s, k)] = v;   // ook extreem sterk telt: dat is een zender pal naast je
                    if (v > VERY_STRONG_DBM) ++veryStrong;
                }
        }
        if (nRadios > 0) radios[0]->endBurst();
        float now = millis() / 1000.0f;
        sweepMs = (micros() - t0) / 1000.0f;
        // Stoorpiek-kanalen neutraal maken: waarde = huidige ruisvloer.
        {
            float fl = detector.floorDbm();
            if (fl > -40 || fl < -140) fl = -120;          // nog niet opgewarmd
            for (int i = 0; i < N_CHANNELS; ++i) if (spurMask[i]) rssi[i] = fl;
        }
        sweepReverse = !sweepReverse;

        // Automatische versterking: sterkste kanaal te hoog → stand omlaag,
        // lang genoeg rustig → stand weer omhoog.
        float maxRssi = -200;
        for (int i = 0; i < N_CHANNELS; ++i) maxRssi = max(maxRssi, rssi[i]);
        bool stepped = false;
        if (maxRssi >= GAIN_UP_DBM) lastLoud = now;          // laatste moment met een redelijk sterk signaal
        if (maxRssi > GAIN_DOWN_DBM) lastStrong = now;       // laatste moment met een zéér sterk signaal
        if (!AUTO_GAIN) {
            // vaste, maximale versterking
        } else if (maxRssi > GAIN_DOWN_DBM && gainStep < GAIN_STEP_MAX && now - lastGainStep >= GAIN_STEP_MIN_S) {
            ++gainStep; stepped = true;                       // stap voor stap omlaag
        } else if (gainStep > 1 && now - lastLoud >= GAIN_UP_AFTER_S) {
            gainStep = 1; stepped = true;                     // rustig: in één keer terug naar maximaal
        }
        if (stepped) {
            lastGainStep = now;
            for (int s = 0; s < nRadios; ++s) radios[s]->setLnaGain(gainStep);
            Serial.printf("[gain] stand G%d (sterkste %.1f dBm)\n", gainStep, maxRssi);
        }

        xSemaphoreTake(lock, portMAX_DELAY);
        DriveMode m = MODES[state.mode];
        if (SENS_MAX) { m.soft = SENS_MAX_SOFT; m.hard = SENS_MAX_HARD; }
        detector.setThresholds(m.soft, m.hard);
        detector.setStrongNearby(AUTO_GAIN && now - lastStrong < 1.0f);   // zender pal naast je → rood
        if (stepped) detector.gainChanged();
        detector.process(rssi, segStart, segLen, nRadios, now);
        detector.snapshot(snap);
        int alarm = snap.alarm;
        float db = snap.alarmDb;
        xSemaphoreGive(lock);

        beeper::update(state.volume == 0 ? 0 : alarm, db, m.soft);
        ++sweepCount;
        ++statSweeps;

        // Meetrit: elke seconde de 5 kanalen die het hoogst boven de ruis uitkwamen
        // (piek over die hele seconde, zodat korte pulsjes niet wegvallen).
        static float secPeak[N_CHANNELS];
        static float secPeakAbs[N_CHANNELS];
        static bool secInit = false;
        if (!secInit) { for (int i = 0; i < N_CHANNELS; ++i) { secPeak[i] = 0; secPeakAbs[i] = -200; } secInit = true; }
        for (int i = 0; i < N_CHANNELS; ++i) {
            float e = detector.excessDb(i);
            if (e > secPeak[i]) { secPeak[i] = e; secPeakAbs[i] = rssi[i]; }
        }
        static uint32_t topT0 = 0;
        if ((SENS_MAX || LOG_TOP) && millis() - topT0 >= 1000) {
            topT0 = millis();
            int best[5] = {-1, -1, -1, -1, -1};
            for (int i = 0; i < N_CHANNELS; ++i) {
                float e = secPeak[i];
                for (int j = 0; j < 5; ++j) {
                    if (best[j] < 0 || e > secPeak[best[j]]) {
                        for (int q = 4; q > j; --q) best[q] = best[q - 1];
                        best[j] = i;
                        break;
                    }
                }
            }
            Serial.printf("[top] t=%lus vloer %.1f dBm |", millis() / 1000, detector.floorDbm());
            for (int j = 0; j < 5; ++j)
                Serial.printf(" %.3f:+%.0f(%.0f)", channelHz(best[j]) / 1e6, secPeak[best[j]],
                              secPeakAbs[best[j]]);   // piek in deze seconde; (…) = dBm op dat moment
            Serial.printf(" | alarm %d\n", alarm);
            for (int i = 0; i < N_CHANNELS; ++i) { secPeak[i] = 0; secPeakAbs[i] = -200; }
        }

        // Elke paar seconden een regel in de seriële log (zoals "dekking" op de Pi).
        if (millis() - statT0 >= STAT_INTERVAL_S * 1000) {
            float el = (millis() - statT0) / 1000.0f;
            int top = -1;
            for (int i = 0; i < N_CHANNELS; ++i)
                if (top < 0 || rssi[i] > rssi[top]) top = i;
            Serial.printf("[scan] %d radio('s), %.1f rondes/s (%.1f ms/ronde), ruisvloer %.1f dBm, "
                          "sterkste %.4f MHz %.1f dBm, gain G%d, alarm %d, zeer sterk %lu, herhaal %lu\n",
                          nRadios, statSweeps / el, sweepMs, detector.floorDbm(),
                          channelHz(top) / 1e6, rssi[top], gainStep, alarm, (unsigned long)veryStrong,
                          (unsigned long)detector.takeRepeatCount());
            veryStrong = 0;
            statT0 = millis();
            statSweeps = 0;
        }
        vTaskDelay(1);   // watchdog tevreden houden
    }
}

void saveSettings() {
    prefs.putUChar("mode", state.mode);
    prefs.putUChar("vol", state.volume);
}
}

void setup() {
    Serial.begin(115200);
    lock = xSemaphoreCreateMutex();
    prefs.begin("priosense", false);
    state.mode = min<int>(prefs.getUChar("mode", 0), N_MODES - 1);
    state.volume = min<int>(prefs.getUChar("vol", DEFAULT_VOLUME), N_VOLUME - 1);

    ui::begin(lcd);
    ui::splash(0.1f, "Starting...");
    beeper::begin();
    beeper::setVolume(VOLUME_LEVELS[state.volume]);

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

    for (int i = 0; i < N_CHANNELS; ++i) frfOf[i] = Sx1278Rssi::frfFor(channelHz(i));
    planScan();
    buildSpurMask();
    buildScanLists();

    if (nRadios == 0) {
        ui::splash(1.0f, "No radio found - check wiring");
        delay(4000);
    } else {
        if (TEST_DOWNLINK) {
            Serial.println("[test] TEST_DOWNLINK staat aan: scant de downlink 390,9-394,1 MHz");
            ui::splash(1.0f, "TEST MODE downlink 392.5 MHz");
            delay(1500);
        } else if (TEST_433) {
            Serial.println("[test] TEST_433 staat aan: scant rond 433,92 MHz (autosleutel-test)");
            ui::splash(1.0f, "TEST MODE 433 MHz - key fob test");
            delay(1500);
        } else {
            ui::splash(1.0f, nRadios == 2 ? "2 radios ready" : "1 radio ready");
        }
        delay(800);
        xTaskCreatePinnedToCore(scanTask, "scan", 8192, nullptr, 3, nullptr, 0);
    }
    ui::drawMain(state);
}

void loop() {
    static uint32_t lastSweep = 0, lastSweepMs = 0, phaseMs = 0;
    if (testMuteAtMs && millis() >= testMuteAtMs) {
        testMuteAtMs = 0;
        if (state.volume == 0) beeper::setVolume(0);
    }
    static bool phase = false;
    uint32_t now = millis();

    switch (ui::pollTouch()) {
        case ui::Touch::City:      if (state.mode != 0) { state.mode = 0; saveSettings(); } break;
        case ui::Touch::Highway:   if (state.mode != 1) { state.mode = 1; saveSettings(); } break;
        case ui::Touch::SoundTap:
            state.volume = (state.volume + 1) % N_VOLUME;     // uit → zacht → middel → hard
            beeper::setVolume(VOLUME_LEVELS[state.volume]);
            saveSettings();
            break;
        case ui::Touch::SoundHold:
            // Staat het geluid uit? Dan op middel testen en daarna weer uit.
            if (state.volume == 0) {
                beeper::setVolume(VOLUME_LEVELS[2]);
                testMuteAtMs = millis() + (uint32_t)(SOUND_TEST_S * 1000) + 200;
            }
            beeper::test();
            break;
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
