#include "beeper.h"
#include "config.h"
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

constexpr int TONE_N = (int)(RATE * BEEP_PULSE_S);
int16_t tone_[TONE_N];               // één piep van BEEP_PULSE_S
int16_t silence_[256];

float gapFor(float frac) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    return BEEP_MAX_GAP_S - frac * (BEEP_MAX_GAP_S - BEEP_MIN_GAP_S);
}

void writeSilence(float seconds) {
    int n = (int)(seconds * RATE);
    size_t w;
    while (n > 0) {
        int k = n > 256 ? 256 : n;
        i2s_write(PORT, silence_, k * sizeof(int16_t), &w, portMAX_DELAY);
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
        writeSilence(gap - BEEP_PULSE_S > 0 ? gap - BEEP_PULSE_S : 0);
    }
}
}

void begin() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
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

    // Piep met korte in-/uitfade (geen klik).
    const int n = TONE_N;
    const int fade = RATE / 500;   // 2 ms
    for (int i = 0; i < n; ++i) {
        float env = 1.0f;
        if (i < fade) env = (float)i / fade;
        if (n - i < fade) env = (float)(n - i) / fade;
        tone_[i] = (int16_t)(32767 * BEEP_VOLUME * env * sinf(2 * M_PI * BEEP_TONE_HZ * i / RATE));
    }
    xTaskCreatePinnedToCore(task, "beeper", 4096, nullptr, 2, nullptr, 1);
}

void update(int alarm, float db, float soft) {
    portENTER_CRITICAL(&mux);
    active = alarm > 0;
    if (active) gapS = gapFor((db - soft) / fmaxf(BEEP_TOP_DB - soft, 1.0f));
    portEXIT_CRITICAL(&mux);
}

void test() {
    portENTER_CRITICAL(&mux);
    testUntilMs = millis() + (uint32_t)(SOUND_TEST_S * 1000);
    portEXIT_CRITICAL(&mux);
}
}
