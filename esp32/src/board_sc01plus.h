// LovyanGFX-configuratie voor de Smart Panlee SC01 Plus (ZX3D50CE08S):
// ST7796 op een 8-bit parallelle bus, FT6336U-touch (FT5x06-familie) op I2C.
// Pinnen volgens het datasheet van de SC01 Plus.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Kleur onder zachte (anti-aliased) randen. Het scherm kan niet teruggelezen
// worden (geen RD-pin), en dan mengde LovyanGFX de randen met zwart: een dun
// zwart randje om knoppen en balken. De UI zet hier de kleur eronder neer.
inline uint32_t& aaUnderRgb888() { static uint32_t c = 0x0B0E12; return c; }

class Panel_ST7796_AA : public lgfx::Panel_ST7796 {
public:
    void writeFillRectAlphaPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w,
                                      uint_fast16_t h, uint32_t argb) override {
        uint32_t a = argb >> 24, u = aaUnderRgb888();
        auto mix = [&](int sh) { return (((argb >> sh) & 0xFF) * a + ((u >> sh) & 0xFF) * (255 - a)) / 255; };
        uint16_t c = ((mix(16) & 0xF8) << 8) | ((mix(8) & 0xFC) << 3) | (mix(0) >> 3);
        writeFillRectPreclipped(x, y, w, h, (uint16_t)((c >> 8) | (c << 8)));   // paneel: 565, bytes omgedraaid
    }
};

class LGFX_SC01Plus : public lgfx::LGFX_Device {
    Panel_ST7796_AA     panel_;
    lgfx::Bus_Parallel8 bus_;
    lgfx::Light_PWM     light_;
    lgfx::Touch_FT5x06  touch_;

public:
    LGFX_SC01Plus() {
        {
            auto cfg = bus_.config();
            cfg.freq_write = 40000000;
            cfg.pin_wr = 47;
            cfg.pin_rd = -1;
            cfg.pin_rs = 0;            // D/C
            cfg.pin_d0 = 9;
            cfg.pin_d1 = 46;
            cfg.pin_d2 = 3;
            cfg.pin_d3 = 8;
            cfg.pin_d4 = 18;
            cfg.pin_d5 = 17;
            cfg.pin_d6 = 16;
            cfg.pin_d7 = 15;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs = -1;
            cfg.pin_rst = 4;
            cfg.pin_busy = -1;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_rotation = 0;
            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }
        {
            auto cfg = light_.config();
            cfg.pin_bl = 45;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        {
            auto cfg = touch_.config();
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            // INT (GPIO 7) niet gebruiken: na opstarten staat de FT6336 soms in
            // "trigger"-stand (korte puls per aanraking); die puls werd gemist en
            // dan reageerde touch pas na een tijdje. Altijd uitlezen werkt meteen.
            cfg.pin_int = -1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 1;
            cfg.i2c_addr = 0x38;
            cfg.pin_sda = 6;
            cfg.pin_scl = 5;
            cfg.freq = 400000;
            touch_.config(cfg);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
