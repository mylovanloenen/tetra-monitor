#include "beeper.h"
#include "config.h"
#include "sounds/bark.h"
#include "sounds/ping.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

namespace beeper {
namespace {
constexpr int RATE = 16000;
constexpr i2s_port_t PORT = I2S_NUM_0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool active = false;
float gapS = BEEP_MAX_GAP_S;
uint32_t testUntilMs = 0;

static_assert(BARK_RATE == RATE && PING_RATE == RATE, "geluiden moeten op 16 kHz staan");
constexpr bool SAMPLED = BEEP_SOUND != SOUND_BLIP;
constexpr int TONE_N = BEEP_SOUND == SOUND_BARK ? BARK_N
                     : BEEP_SOUND == SOUND_PING ? PING_N : (int)(RATE * BEEP_PULSE_S);
constexpr float TONE_S = (float)TONE_N / RATE;
// Bij een opgenomen geluid nooit sneller dan het geluid zelf plus een kort stilte-gaatje.
constexpr float MIN_GAP_S = SAMPLED ? TONE_S + 0.06f : BEEP_MIN_GAP_S;
// Stereo (L,R om en om): de NS4168 op de SC01 Plus speelt maar één kanaal af,
// afhankelijk van hoe hij op het board is ingesteld. Op beide kanalen hetzelfde
// sturen werkt altijd.
float   base_[TONE_N];               // één blip op vol volume (-1..1)
int16_t tone_[2 * TONE_N];           // dezelfde blip op het ingestelde volume
int16_t silence_[2 * 256];

float gapFor(float frac) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    float g = BEEP_MAX_GAP_S - frac * (BEEP_MAX_GAP_S - BEEP_MIN_GAP_S);
    return g < MIN_GAP_S ? MIN_GAP_S : g;
}

void writeSilence(float seconds) {
    int n = (int)(seconds * RATE);
    size_t w;
    while (n > 0) {
        int k = n > 256 ? 256 : n;
        i2s_write(PORT, silence_, 2 * k * sizeof(int16_t), &w, portMAX_DELAY);
        n -= k;
    }
}

void task(void*) {
    size_t w;
    for (;;) {
        portENTER_CRITICAL(&mux);
        bool on = active;
        float gap = gapS;
        int32_t testLeft = (int32_t)(testUntilMs - millis());
        portEXIT_CRITICAL(&mux);

        if (testLeft > 0) {
            float total = SOUND_TEST_S * 1000.0f;
            gap = gapFor(1.0f - testLeft / total);
            on = true;
        }
        if (!on) { writeSilence(0.05f); continue; }
        i2s_write(PORT, tone_, sizeof(tone_), &w, portMAX_DELAY);
        writeSilence(gap - TONE_S > 0 ? gap - TONE_S : 0);
    }
}
}

void begin() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.tx_desc_auto_clear = true;
    i2s_driver_install(PORT, &cfg, 0, nullptr);
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = PIN_I2S_BCLK;
    pins.ws_io_num = PIN_I2S_LRCK;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(PORT, &pins);

    // "Blip": toon die omhoog glijdt (BEEP_F_START → BEEP_F_END) met een
    // zachte, ronde omhullende (sin^1.5), dus geen klik aan het begin of eind.
    const int n = TONE_N;
    float phase = 0.0f;
    const int16_t* smp = BEEP_SOUND == SOUND_BARK ? BARK : PING;
    for (int i = 0; i < n && SAMPLED; ++i) base_[i] = smp[i] / 32768.0f;
    for (int i = 0; i < n && !SAMPLED; ++i) {
        float t = (float)i / n;
        float f = BEEP_F_START + (BEEP_F_END - BEEP_F_START) * t;
        phase += 2 * M_PI * f / RATE;
        float env = powf(sinf(M_PI * t), 1.5f);
        base_[i] = env * sinf(phase);
    }
    setVolume(VOLUME_LEVELS[DEFAULT_VOLUME]);
    xTaskCreatePinnedToCore(task, "beeper", 4096, nullptr, 2, nullptr, 1);
}

float lerpGap(float a, float b, float frac) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    float g = a + (b - a) * frac;
    return g < MIN_GAP_S ? MIN_GAP_S : g;
}

void update(int alarm, float db, float soft, float hard) {
    portENTER_CRITICAL(&mux);
    active = alarm > 0;
    if (alarm >= 2 || db >= hard)
        gapS = lerpGap(BEEP_RED_SLOW_S, BEEP_RED_FAST_S, (db - hard) / fmaxf(BEEP_TOP_DB - hard, 1.0f));
    else if (active)
        gapS = lerpGap(BEEP_ORANGE_SLOW_S, BEEP_ORANGE_FAST_S, (db - soft) / fmaxf(hard - soft, 1.0f));
    portEXIT_CRITICAL(&mux);
}

void setVolume(float v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    for (int i = 0; i < TONE_N; ++i) {
        int16_t s = (int16_t)(32767 * v * base_[i]);
        tone_[2 * i] = s;
        tone_[2 * i + 1] = s;
    }
}

void test() {
    portENTER_CRITICAL(&mux);
    testUntilMs = millis() + (uint32_t)(SOUND_TEST_S * 1000);
    portEXIT_CRITICAL(&mux);
}
}
