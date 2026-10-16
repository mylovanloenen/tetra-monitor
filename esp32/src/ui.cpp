#include "ui.h"
#include <Arduino.h>
#include "config.h"
#include "fonts/brand_fonts.h"
#include <math.h>

namespace ui {
namespace {
Display* L = nullptr;

// Palet van het hoofdscherm-ontwerp (SCREEN in tetra_monitor.py).
uint16_t rgb(uint32_t hex) { return lgfx::color565(hex >> 16, (hex >> 8) & 0xFF, hex & 0xFF); }
// Kleur `fg` met dekking a (0–1) over `bg` (beide als 0xRRGGBB).
uint16_t blend(float a, uint32_t fg, uint32_t bg) {
    auto mix = [&](int sh) { return (int)(((fg >> sh) & 0xFF) * a + ((bg >> sh) & 0xFF) * (1 - a)); };
    return lgfx::color565(mix(16), mix(8), mix(0));
}
const uint16_t BG = rgb(0x0B0E12), INK = rgb(0xE9EDF2), MUTED = rgb(0xAEB6C2),
               CHIP = rgb(0x151A21), CHIP_LINE = rgb(0x2A313B), SEG_OFF = rgb(0x1C222A),
               GREEN = rgb(0x2FBF71), GREEN_DIM = rgb(0x1E5A3A), YELLOW = rgb(0xFFD23F),
               RED = rgb(0xFF4136), MUTE_BG = rgb(0x3A1414), MUTE_INK = rgb(0xFFB3AE),
               BLUE = rgb(0x3AA0FF);

// Merklettertypen (anti-aliased, uit tools/make_fonts.py). Alleen opnieuw
// laden als er echt van font gewisseld wordt.
const uint8_t* loadedFont = nullptr;
void useFont(const uint8_t* f) {
    if (f != loadedFont) { L->loadFont(f); loadedFont = f; }
}

constexpr int W = 480, H = 320, M = 16;
constexpr int TOP_Y = 16, CHIP_H = 56, PILL_H = 48, SOUND_H = 56;
constexpr int METER_Y = TOP_Y + CHIP_H + 16, METER_H = H - M - METER_Y, N_SEG = 10, SEG_GAP = 6;

struct Rect { int x, y, w, h; bool hit(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h; } };
Rect chipR, cityR, highwayR, soundR, dotR;

// ── Icoontjes op een 24×24-raster, zoals in het ontwerp ─────────────────────
using IconFn = void (*)(float x, float y, float k, uint16_t c);
void line(float x, float y, float k, float x0, float y0, float x1, float y1, uint16_t c) {
    L->drawWideLine(x + x0 * k, y + y0 * k, x + x1 * k, y + y1 * k, k, c);
}
void arc(float x, float y, float k, float cx, float cy, float r, float a0, float a1, uint16_t c) {
    const int n = 10;
    for (int i = 0; i < n; ++i) {
        float t0 = (a0 + (a1 - a0) * i / n) * DEG_TO_RAD, t1 = (a0 + (a1 - a0) * (i + 1) / n) * DEG_TO_RAD;
        line(x, y, k, cx + r * cosf(t0), cy - r * sinf(t0), cx + r * cosf(t1), cy - r * sinf(t1), c);
    }
}
void iconCity(float x, float y, float k, uint16_t c) {
    line(x, y, k, 3, 21, 21, 21, c);
    line(x, y, k, 5, 21, 5, 8, c);   line(x, y, k, 5, 8, 10, 5, c);   line(x, y, k, 10, 5, 10, 21, c);
    line(x, y, k, 10, 10, 19, 10, c); line(x, y, k, 19, 10, 19, 21, c);
    line(x, y, k, 13, 14, 16, 14, c); line(x, y, k, 13, 17, 16, 17, c);
}
void iconHighway(float x, float y, float k, uint16_t c) {
    line(x, y, k, 8, 3, 4, 21, c);
    line(x, y, k, 16, 3, 20, 21, c);
    for (int yy : {4, 11, 18}) line(x, y, k, 12, yy, 12, yy + 3, c);
}
void speaker(float x, float y, float k, uint16_t c) {
    const float p[][2] = {{11, 5}, {6, 9}, {3, 9}, {3, 15}, {6, 15}, {11, 19}, {11, 5}};
    for (int i = 0; i < 6; ++i) line(x, y, k, p[i][0], p[i][1], p[i + 1][0], p[i + 1][1], c);
}
void iconSoundOn(float x, float y, float k, uint16_t c) {
    speaker(x, y, k, c);
    arc(x, y, k, 11.93f, 12, 5, -44.4f, 44.4f, c);
    arc(x, y, k, 11.86f, 12, 10, -44.4f, 44.4f, c);
}
void iconSoundOff(float x, float y, float k, uint16_t c) {
    speaker(x, y, k, c);
    line(x, y, k, 22, 9, 16, 15, c);
    line(x, y, k, 16, 9, 22, 15, c);
}

int pillWidth(const char* text, int iconPx) {
    useFont(FONT_BUTTON);
    return 16 + iconPx + 8 + L->textWidth(text) + 16;
}

// bg < 0 = geen eigen vlak; `under` is dan de kleur eronder (voor de
// anti-aliasing van de tekst: het scherm kan niet teruggelezen worden).
void drawPill(const Rect& r, const char* text, IconFn icon, int iconPx, int radius,
              int bg, uint16_t ink, int line_, uint16_t under = BG) {
    if (bg >= 0) L->fillSmoothRoundRect(r.x, r.y, r.w, r.h, radius, bg);
    if (line_ >= 0) L->drawRoundRect(r.x, r.y, r.w, r.h, radius, line_);
    icon(r.x + 16, r.y + (r.h - iconPx) / 2.0f, iconPx / 24.0f, ink);
    useFont(FONT_BUTTON);
    L->setTextColor(ink, bg >= 0 ? (uint16_t)bg : under);
    L->setTextDatum(lgfx::middle_left);
    L->drawString(text, r.x + 16 + iconPx + 8, r.y + r.h / 2 + 1);
}

void drawMode(int mode) {
    L->fillSmoothRoundRect(chipR.x, chipR.y, chipR.w, chipR.h, 14, CHIP);
    L->drawRoundRect(chipR.x, chipR.y, chipR.w, chipR.h, 14, CHIP_LINE);
    drawPill(cityR, "City", iconCity, 20, 10, mode == 0 ? INK : -1, mode == 0 ? BG : MUTED, -1, CHIP);
    drawPill(highwayR, "Highway", iconHighway, 20, 10, mode == 1 ? INK : -1, mode == 1 ? BG : MUTED, -1, CHIP);
}

void drawSound(bool muted) {
    L->fillRect(soundR.x, soundR.y, soundR.w, soundR.h, BG);
    if (muted) drawPill(soundR, "Sound off", iconSoundOff, 22, 14, MUTE_BG, MUTE_INK, RED);
    else       drawPill(soundR, "Sound on", iconSoundOn, 22, 14, CHIP, INK, CHIP_LINE);
}

void drawDot(bool ok, bool phase) {
    uint16_t c = ok ? (phase ? GREEN : GREEN_DIM) : RED;
    L->fillSmoothCircle(dotR.x + 6, dotR.y + 6, 5, c);
}

// dB boven ruis → aantal brandende segmenten (segments_lit in tetra_monitor.py).
int segmentsLit(float level, float soft, float hard) {
    if (level <= 0) return 0;
    if (level < soft) return max(1, min(3, (int)ceilf(3 * level / soft)));
    if (level < hard) return 4 + min(2, (int)(3 * (level - soft) / fmaxf(1e-6f, hard - soft)));
    return min(N_SEG, 7 + (int)((level - hard) / 4));
}

void drawSegments(int lit) {
    float segW = (W - 2 * M - (N_SEG - 1) * SEG_GAP) / (float)N_SEG;
    for (int i = 0; i < N_SEG; ++i) {
        uint16_t c = i >= lit ? SEG_OFF : i < 3 ? GREEN : i < 6 ? YELLOW : RED;
        L->fillRect(M + i * (segW + SEG_GAP) - 1, METER_Y - 1, segW + 2, METER_H + 2, BG);
        L->fillSmoothRoundRect(M + i * (segW + SEG_GAP), METER_Y, segW, METER_H, 8, c);
    }
}

// Het priosense-logo: punt met drie zendbogen (draw_brand_mark).
void brandMark(float cx, float cy, float s, uint32_t colorHex) {
    uint16_t color = rgb(colorHex);
    struct { float r, w, a; } arcs[] = {{9.6f, 3.52f, 1.0f}, {17.28f, 2.75f, 0.765f}, {24.96f, 1.98f, 0.53f}};
    for (auto& a : arcs) {
        uint16_t c = blend(a.a, colorHex, 0x0B0E12);
        float r = a.r * s, hw = a.w * s / 2;
        // Bogen van 35° tot 145° (boven), met ronde uiteinden.
        L->fillArc(cx, cy, r + hw, r - hw, 215, 325, c);
        for (float deg : {35.0f, 145.0f}) {
            float t = deg * DEG_TO_RAD;
            L->fillSmoothCircle(cx + r * cosf(t), cy - r * sinf(t), hw, c);
        }
    }
    L->fillSmoothCircle(cx, cy, 3.2f * s, color);
}

// UI-staat die al op het scherm staat (om alleen verschillen te tekenen).
int shownMode = -1, shownLit = -1;
bool shownMuted = false, shownOk = false, shownPhase = false;
float stickyFreqIdx = -1;

// Touch: tik = bij loslaten, Sound vasthouden ≥ 1 s = test.
bool wasDown = false, holdFired = false;
uint32_t downMs = 0;
int downX = 0, downY = 0;
}

void begin(Display& lcd) {
    L = &lcd;
    L->init();
    L->setRotation(SCREEN_ROTATION);
    L->setBrightness(BACKLIGHT);
    L->fillScreen(BG);

    // Lay-out berekenen (zelfde opbouw als de compacte Pi-modus).
    int cityW = pillWidth("City", 20), hwW = pillWidth("Highway", 20);
    chipR = {M, TOP_Y, 4 + cityW + 4 + hwW + 4, CHIP_H};
    cityR = {M + 4, TOP_Y + 4, cityW, PILL_H};
    highwayR = {M + 4 + cityW + 4, TOP_Y + 4, hwW, PILL_H};
    int soundW = max(pillWidth("Sound off", 22), pillWidth("Sound on", 22));
    soundR = {W - M - soundW, TOP_Y, soundW, SOUND_H};
    int dotX = (chipR.x + chipR.w + soundR.x) / 2 - 6;
    dotR = {dotX, TOP_Y + CHIP_H / 2 - 6, 12, 12};
}

void splash(float pct, const char* text) {
    L->startWrite();
    L->fillScreen(BG);
    const int m = 28;
    // klein logo + woordmerk linksboven
    const float s = 0.95f;
    brandMark(m + 20.5f * s, 44, s, 0x3AA0FF);
    L->setTextDatum(lgfx::baseline_left);
    useFont(FONT_WORDMARK);
    L->setTextColor(INK, BG);
    L->drawString("priosense", m + 41 * s + 8, 50);

    // "Hear what / you can't hear yet." — "yet." cursief in merkblauw
    useFont(FONT_HERO);
    L->setTextColor(INK, BG);
    L->drawString("Hear what", m, 150);
    int w1 = L->drawString("you can't hear ", m, 204);
    useFont(FONT_ACCENT);
    L->setTextColor(BLUE, BG);
    L->drawString("yet.", m + w1, 204);

    if (pct >= 0) {
        const int bw = W - 2 * m, bh = 4, by = H - 52;
        L->fillSmoothRoundRect(m, by, bw, bh, bh / 2, CHIP_LINE);
        if (pct > 0) L->fillSmoothRoundRect(m, by, max(bh, (int)(bw * fminf(1, pct))), bh, bh / 2, BLUE);
        if (text) {
            useFont(FONT_STATUS);
            L->setTextColor(MUTED, BG);
            L->setTextDatum(lgfx::top_left);
            L->drawString(text, m, by + bh + 10);
        }
    }
    L->endWrite();
}

void drawMain(const State& st) {
    L->startWrite();
    L->fillScreen(BG);
    drawMode(st.mode);
    drawSound(st.muted);
    drawDot(st.scanning, false);
    drawSegments(0);
    L->endWrite();
    shownMode = st.mode; shownMuted = st.muted; shownLit = 0;
    shownOk = st.scanning; shownPhase = false;
}

void update(const State& st, const DetectorSnapshot& snap, bool phase) {
    L->startWrite();
    if (st.mode != shownMode) { drawMode(st.mode); shownMode = st.mode; }
    if (st.muted != shownMuted) { drawSound(st.muted); shownMuted = st.muted; }
    if (st.scanning != shownOk || phase != shownPhase) {
        drawDot(st.scanning, phase); shownOk = st.scanning; shownPhase = phase;
    }

    // Meter blijft bij het kanaal dat al getoond wordt zolang dat nog actief
    // is, anders springt hij bij meerdere zenders heen en weer.
    int idx = (int)stickyFreqIdx;
    if (idx < 0 || snap.levels[idx] <= 0) {
        idx = -1;
        float best = 0;
        for (int i = 0; i < N_CHANNELS; ++i)
            if (snap.levels[i] > best) { best = snap.levels[i]; idx = i; }
    }
    stickyFreqIdx = idx;
    const DriveMode& m = MODES[st.mode];
    int lit = snap.overload ? N_SEG : segmentsLit(idx >= 0 ? snap.levels[idx] : 0, m.soft, m.hard);
    if (lit != shownLit) { drawSegments(lit); shownLit = lit; }
    L->endWrite();
}

Touch pollTouch() {
    int32_t x, y;
    bool down = L->getTouch(&x, &y);
    uint32_t now = millis();
    Touch result = Touch::None;
    if (down && !wasDown) {
        downMs = now; downX = x; downY = y; holdFired = false;
    } else if (down && wasDown) {
        if (!holdFired && soundR.hit(downX, downY) && now - downMs >= 1000) {
            holdFired = true;
            result = Touch::SoundHold;
        }
    } else if (!down && wasDown && !holdFired) {
        if (cityR.hit(downX, downY)) result = Touch::City;
        else if (highwayR.hit(downX, downY)) result = Touch::Highway;
        else if (soundR.hit(downX, downY)) result = Touch::SoundTap;
    }
    wasDown = down;
    return result;
}
}
