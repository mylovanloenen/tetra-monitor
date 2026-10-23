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
constexpr uint32_t BG_HEX = 0x0B0E12, INK_HEX = 0xE9EDF2, CHIP_HEX = 0x151A21,
                   CHIP_LINE_HEX = 0x2A313B, MUTE_BG_HEX = 0x3A1414;
// Kleur onder de volgende zachte randen (zie board_sc01plus.h).
void under(uint32_t hex) {
#if !defined(PRIOSENSE_SIM)
    aaUnderRgb888() = hex;
#else
    (void)hex;
#endif
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

// ── Waterval ─────────────────────────────────────────────────────────────────
// 3 px per kanaal, ~70 rijen per seconde (één per meetronde): een TETRA-pulsje
// (14 ms) is één streepje, een pratende portofoon een stippellijn (elke ~57 ms).
// "Lopende streep": elke nieuwe rij komt onder de vorige, aan de onderkant weer
// bovenaan beginnen. Het hele vlak laten schuiven stuurt 30× per seconde ~160 kB
// naar het scherm, en die datadraden stoorden de Ra-02 (ruisvloer +3 dB, geel).
constexpr int WF_COL = 3, WF_W = N_CHANNELS * WF_COL, WF_X = (W - WF_W) / 2;
constexpr int WF_Y = METER_Y, WF_H = H - WF_Y - 14;      // onderaan ruimte voor MHz-labels
constexpr int WF_Q = 24;                                   // rijen-wachtrij scan → scherm
uint8_t wfQueue[WF_Q][N_CHANNELS];
volatile int wfHead = 0, wfCount = 0;
portMUX_TYPE wfMux = portMUX_INITIALIZER_UNLOCKED;
int wfCursor = 0;                       // volgende rij op het scherm
lgfx::swap565_t wfPal[64];              // dB 0..63 → kleur
bool wfPalReady = false;
constexpr uint8_t WF_MASKED = 255;

void wfInit() {
    if (wfPalReady) return;
    // Kleuren: ruis (≤ 6 dB) donker, dan blauw → cyaan → geel → rood → wit.
    struct Stop { float db; uint8_t r, g, b; };
    const Stop st[] = {{0, 6, 8, 12}, {6, 8, 12, 24}, {12, 20, 60, 170}, {18, 0, 190, 230},
                       {24, 255, 220, 40}, {32, 255, 60, 30}, {44, 255, 255, 255}, {63, 255, 255, 255}};
    for (int d = 0; d < 64; ++d) {
        int k = 0;
        while (k < 6 && d > st[k + 1].db) ++k;
        float t = (d - st[k].db) / (st[k + 1].db - st[k].db);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        wfPal[d] = lgfx::swap565_t(st[k].r + t * (st[k + 1].r - st[k].r),
                                   st[k].g + t * (st[k + 1].g - st[k].g),
                                   st[k].b + t * (st[k + 1].b - st[k].b));
    }
    wfPalReady = true;
}

// Nieuwe rijen meteen op het scherm zetten (alleen die rijen + een witte "nu"-streep).
void wfDrain(bool draw) {
    static uint8_t row[N_CHANNELS];
    static lgfx::swap565_t line[WF_W];
    const lgfx::swap565_t masked(28, 32, 40);
    bool any = false;
    for (;;) {
        portENTER_CRITICAL(&wfMux);
        bool have = wfCount > 0;
        if (have) {
            int idx = (wfHead - wfCount + WF_Q) % WF_Q;
            memcpy(row, wfQueue[idx], N_CHANNELS);
            --wfCount;
        }
        portEXIT_CRITICAL(&wfMux);
        if (!have) break;
        if (!draw) continue;
        lgfx::swap565_t* d = line;
        for (int i = 0; i < N_CHANNELS; ++i) {
            lgfx::swap565_t c = row[i] == WF_MASKED ? masked : wfPal[row[i] & 63];
            for (int x = 0; x < WF_COL; ++x) *d++ = c;
        }
        L->pushImage(WF_X, WF_Y + wfCursor, WF_W, 1, line);
        wfCursor = (wfCursor + 1) % WF_H;
        any = true;
    }
    if (any) L->drawFastHLine(WF_X, WF_Y + wfCursor, WF_W, INK);   // "nu"-streep
}

int wfShownAlarm = -1;
void wfSideBars(int alarm) {
    uint16_t c = alarm >= 2 ? RED : alarm == 1 ? YELLOW : SEG_OFF;
    L->fillRect(M, WF_Y, WF_X - M - 6, WF_H, c);
    L->fillRect(WF_X + WF_W + 6, WF_Y, W - M - (WF_X + WF_W + 6), WF_H, c);
}

void wfFrame() {
    L->fillRect(0, WF_Y, W, H - WF_Y, BG);
    wfSideBars(0);
    // MHz-streepjes en -labels onder de waterval
    L->setFont(&fonts::Font0);
    loadedFont = nullptr;
    L->setTextColor(MUTED, BG);
    L->setTextDatum(lgfx::top_center);
    for (int mhz = 381; mhz <= 384; ++mhz) {
        int ch = (int)((mhz * 1000000LL - (long long)BAND_START_HZ) / CHANNEL_HZ);
        if (ch < 0 || ch >= N_CHANNELS) continue;
        int x = WF_X + ch * WF_COL + WF_COL / 2;
        L->drawFastVLine(x, WF_Y + WF_H, 3, MUTED);
        char s[8]; snprintf(s, sizeof(s), "%d", mhz);
        L->drawString(s, x, WF_Y + WF_H + 4);
    }
    wfShownAlarm = 0;
}

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
// Speaker met 1–3 golfjes, naar het volume.
template <int N> void iconSoundLevel(float x, float y, float k, uint16_t c) {
    speaker(x, y, k, c);
    const float r[] = {4.0f, 7.5f, 11.0f};
    for (int i = 0; i < N; ++i) arc(x, y, k, 11.9f, 12, r[i], -44.4f, 44.4f, c);
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
              int bg, uint16_t ink, int line_, uint16_t under_ = BG,
              uint32_t aroundHex = BG_HEX, uint32_t fillHex = BG_HEX) {
    uint16_t under = under_;
    ::ui::under(aroundHex);
    if (bg >= 0) L->fillSmoothRoundRect(r.x, r.y, r.w, r.h, radius, bg);
    ::ui::under(bg >= 0 ? fillHex : aroundHex);       // icoon staat op het vlak
    if (line_ >= 0) L->drawRoundRect(r.x, r.y, r.w, r.h, radius, line_);
    icon(r.x + 16, r.y + (r.h - iconPx) / 2.0f, iconPx / 24.0f, ink);
    useFont(FONT_BUTTON);
    L->setTextColor(ink, bg >= 0 ? (uint16_t)bg : under);
    L->setTextDatum(lgfx::middle_left);
    L->drawString(text, r.x + 16 + iconPx + 8, r.y + r.h / 2 + 1);
}

void drawMode(int mode) {
    under(BG_HEX);
    L->fillSmoothRoundRect(chipR.x, chipR.y, chipR.w, chipR.h, 14, CHIP);
    L->drawRoundRect(chipR.x, chipR.y, chipR.w, chipR.h, 14, CHIP_LINE);
    drawPill(cityR, "City", iconCity, 20, 10, mode == 0 ? INK : -1, mode == 0 ? BG : MUTED, -1, CHIP,
             CHIP_HEX, INK_HEX);
    drawPill(highwayR, "Highway", iconHighway, 20, 10, mode == 1 ? INK : -1, mode == 1 ? BG : MUTED, -1, CHIP,
             CHIP_HEX, INK_HEX);
}

const char* const SOUND_LABEL[] = {"Sound off", "Sound low", "Sound mid", "Sound high"};
const IconFn SOUND_ICON[] = {iconSoundOff, iconSoundLevel<1>, iconSoundLevel<2>, iconSoundLevel<3>};

void drawSound(int volume) {
    L->fillRect(soundR.x, soundR.y, soundR.w, soundR.h, BG);
    if (volume <= 0) drawPill(soundR, SOUND_LABEL[0], iconSoundOff, 22, 14, MUTE_BG, MUTE_INK, RED, BG,
                              BG_HEX, MUTE_BG_HEX);
    else drawPill(soundR, SOUND_LABEL[volume], SOUND_ICON[volume], 22, 14, CHIP, INK, CHIP_LINE, BG,
                  BG_HEX, CHIP_HEX);
}

void drawDot(bool ok, bool phase) {
    uint16_t c = ok ? (phase ? GREEN : GREEN_DIM) : RED;
    under(BG_HEX);
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
    under(BG_HEX);
    float segW = (W - 2 * M - (N_SEG - 1) * SEG_GAP) / (float)N_SEG;
    for (int i = 0; i < N_SEG; ++i) {
        uint16_t c = i >= lit ? SEG_OFF : i < 3 ? GREEN : i < 6 ? YELLOW : RED;
        L->fillRect(M + i * (segW + SEG_GAP) - 1, METER_Y - 1, segW + 2, METER_H + 2, BG);
        L->fillSmoothRoundRect(M + i * (segW + SEG_GAP), METER_Y, segW, METER_H, 8, c);
    }
}

// Het priosense-logo: punt met drie zendbogen (draw_brand_mark).
void brandMark(float cx, float cy, float s, uint32_t colorHex) {
    under(BG_HEX);
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
int shownVolume = -1;
bool shownOk = false, shownPhase = false;

// Touch: tik = bij loslaten, Sound vasthouden ≥ 1 s = test.
bool wasDown = false, holdFired = false;
uint32_t downMs = 0;
int downX = 0, downY = 0;
}

void begin(Display& lcd, int rotation) {
    L = &lcd;
    L->init();
    L->setRotation(rotation);
    L->setBrightness(BACKLIGHT);
    L->fillScreen(BG);

    // Lay-out berekenen (zelfde opbouw als de compacte Pi-modus).
    int cityW = pillWidth("City", 20), hwW = pillWidth("Highway", 20);
    chipR = {M, TOP_Y, 4 + cityW + 4 + hwW + 4, CHIP_H};
    cityR = {M + 4, TOP_Y + 4, cityW, PILL_H};
    highwayR = {M + 4 + cityW + 4, TOP_Y + 4, hwW, PILL_H};
    int soundW = 0;
    for (const char* s : SOUND_LABEL) soundW = max(soundW, pillWidth(s, 22));
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
        under(BG_HEX);
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

bool shownWaterfall = false;

void pushWaterfallRow(const float* excessDb, const bool* masked) {
    portENTER_CRITICAL(&wfMux);
    uint8_t* r = wfQueue[wfHead];
    for (int i = 0; i < N_CHANNELS; ++i) {
        if (masked && masked[i]) { r[i] = WF_MASKED; continue; }
        float d = excessDb[i];
        r[i] = d <= 0 ? 0 : d >= 63 ? 63 : (uint8_t)d;
    }
    wfHead = (wfHead + 1) % WF_Q;
    if (wfCount < WF_Q) ++wfCount;      // vol: oudste rij valt weg
    portEXIT_CRITICAL(&wfMux);
}

void drawMain(const State& st) {
    L->startWrite();
    L->fillScreen(BG);
    drawMode(st.mode);
    drawSound(st.volume);
    drawDot(st.scanning, false);
    if (st.waterfall) { wfInit(); wfFrame(); wfCursor = 0; }
    else drawSegments(0);
    L->endWrite();
    shownMode = st.mode; shownVolume = st.volume; shownLit = 0;
    shownOk = st.scanning; shownPhase = false; shownWaterfall = st.waterfall;
}

void update(const State& st, const DetectorSnapshot& snap, bool phase) {
    if (st.waterfall != shownWaterfall) { drawMain(st); }
    L->startWrite();
    if (st.mode != shownMode) { drawMode(st.mode); shownMode = st.mode; }
    if (st.volume != shownVolume) { drawSound(st.volume); shownVolume = st.volume; }
    if (st.scanning != shownOk || phase != shownPhase) {
        drawDot(st.scanning, phase); shownOk = st.scanning; shownPhase = phase;
    }

    // Balk = sterkste van álle kanalen (elk kanaal houdt z'n piek 4 s vast en zakt
    // dan langzaam), en nooit lager dan wat het alarm/geluid zegt. Zo vertellen
    // balk en piepjes altijd hetzelfde, ook als steeds andere kanalen zenden.
    float level = 0;
    for (int i = 0; i < N_CHANNELS; ++i) level = fmaxf(level, snap.levels[i]);
    const DriveMode& m = MODES[st.mode];
    int lit = segmentsLit(level, m.soft, m.hard);
    if (snap.alarm >= 1) lit = max(lit, segmentsLit(fmaxf(snap.alarmDb, m.soft), m.soft, m.hard));
    if (snap.alarm >= 2) lit = max(lit, 7);
    if (snap.haze) lit = max(lit, 5);          // waas: tot in het geel
    if (snap.overload) lit = N_SEG;            // zender pal naast je: alles rood
    if (st.waterfall) {
        // Waterval: zijbalken in de alarmkleur, en alle nieuwe rijen erin.
        int a = snap.overload ? 2 : snap.alarm;
        if (a != wfShownAlarm) { wfSideBars(a); wfShownAlarm = a; }
        wfDrain(true);
    } else {
        wfDrain(false);   // wachtrij leeg houden, ook als de waterval niet zichtbaar is
        if (lit != shownLit) { drawSegments(lit); shownLit = lit; }
    }
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
        else if (downY >= METER_Y - 8) result = Touch::View;   // tik onderin: meter ↔ waterval
    }
    wasDown = down;
    return result;
}
}
