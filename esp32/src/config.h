// PrioSense ESP32 — alle instellingen op één plek.
//
// WETTELIJK: dit apparaat ONTVANGT alleen. Het meet per kanaal de
// signaalsterkte (RSSI) en decodeert niets. De radiochips worden nooit in
// zendmodus gezet (zie sx1278_rssi.cpp).
#pragma once
#include <stdint.h>

// ── Pinnen ──────────────────────────────────────────────────────────────────
// EXT-connector van de SC01 Plus (8-pins PicoBlade):
//   1 = 5V   → IN van de AMS1117 (die maakt 3,3V voor de radio's)
//   2 = GND  → GND van AMS1117 + beide Ra-02's
//   3..8 hieronder
constexpr int PIN_RADIO_NSS_A = 10;   // EXT 3 — NSS van radio A
constexpr int PIN_RADIO_MOSI  = 11;   // EXT 4 — MOSI van beide radio's
constexpr int PIN_RADIO_SCK   = 12;   // EXT 5 — SCK van beide radio's
constexpr int PIN_RADIO_MISO  = 13;   // EXT 6 — MISO van beide radio's
constexpr int PIN_RADIO_NSS_B = 14;   // EXT 7 — NSS van radio B
constexpr int PIN_RADIO_RESET = 21;   // EXT 8 — RESET van beide radio's

// Ingebouwde versterker (NS4168, I2S) → SPK-connector met de 8Ω-speaker.
constexpr int PIN_I2S_BCLK = 36;
constexpr int PIN_I2S_LRCK = 35;
constexpr int PIN_I2S_DOUT = 37;

// ── Scherm ──────────────────────────────────────────────────────────────────
// 1 of 3: liggend (480×320). Staat het beeld op z'n kop, zet dan de andere.
constexpr int SCREEN_ROTATION = 1;
constexpr int BACKLIGHT       = 255;  // 0–255

// ── Band ────────────────────────────────────────────────────────────────────
// C2000-uplink (portofoons/voertuigen), 25 kHz-kanalen. Zelfde venster als de
// Pi-versie: 3,2 MHz rond 382,5 MHz → 380,900 … 384,075 MHz (128 kanalen).
// Het scannen begint in het midden en loopt naar beneden én naar boven.
constexpr uint32_t CENTER_HZ     = 382500000;
constexpr uint32_t CHANNEL_HZ    = 25000;
constexpr int      N_CHANNELS    = 128;
constexpr int      CENTER_IDX    = N_CHANNELS / 2;    // kanaal 64 = 382,500 MHz
constexpr uint32_t BAND_START_HZ = CENTER_HZ - CENTER_IDX * CHANNEL_HZ;   // 380,900 MHz

// Hoe lang de radio op een kanaal blijft voor de RSSI-meting (µs). Korter =
// sneller rond, maar de meting moet wel stabiel zijn: bij binnenkomst van de
// onderdelen afregelen (zie README, "Afregelen").
constexpr uint32_t DWELL_US = 250;

// ── Detectie (zelfde opzet als tetra_monitor.py) ────────────────────────────
// LET OP: de dB-drempels komen van de Pi/SDR-versie. Een RSSI-chip meet
// anders, dus deze getallen moeten bij de eerste testrit opnieuw afgesteld
// worden (README, "Afregelen").
struct DriveMode { const char* name; float soft; float hard; };
constexpr DriveMode MODES[] = {
    {"City",    35.0f, 45.0f},   // Stad: druk RF → minder gevoelig
    {"Highway", 25.0f, 35.0f},   // Snelweg: gevoeliger
};
constexpr int N_MODES = 2;

constexpr int   CFAR_HALF_CHANS   = 12;     // lokale ruis = mediaan van ±12 buren
constexpr float CHAN_SMOOTH_A     = 0.20f;  // tijdmiddeling per kanaal
constexpr float PEAK_TAU_S        = 0.3f;   // piek-hold van korte bursts
constexpr float HANG_TIME_S       = 4.0f;   // balk blijft staan na laatste contact
constexpr float RELEASE_DB_S      = 12.0f;  // daarna zakt de balk zo snel
constexpr float BLACKLIST_S       = 20.0f;  // zo lang continu actief = storing
constexpr float UNBLACKLIST_S     = 15.0f;  // zo lang stil = weer meedoen
constexpr float HAZE_RISE_DB      = 10.0f;  // ruisvloer zoveel boven normaal = sterk signaal vlakbij
constexpr float OVERLOAD_DBM      = -35.0f; // RSSI hierboven = zender pal naast je
constexpr float OVERLOAD_HOLD_S   = 5.0f;   // oversturing/waas zo lang vasthouden
constexpr float FLOOR_BASE_UP_DB_S = 0.05f; // normale vloer stijgt heel traag…
constexpr float ALARM_TAIL_S      = 2.0f;   // korte nahang na de laatste detectie
constexpr int   WARMUP_SWEEPS     = 40;     // rondes om de ruisvloer op te bouwen

// ── Geluid ──────────────────────────────────────────────────────────────────
constexpr float BEEP_PULSE_S   = 0.06f;  // duur van één piep
constexpr float BEEP_MAX_GAP_S = 0.65f;  // pauze net boven de oranje drempel
constexpr float BEEP_MIN_GAP_S = 0.08f;  // pauze bij een heel sterk signaal
constexpr float BEEP_TOP_DB    = 50.0f;  // hier is het tempo maximaal
constexpr int   BEEP_TONE_HZ   = 2700;
constexpr float BEEP_VOLUME    = 0.5f;   // 0–1
constexpr float SOUND_TEST_S   = 4.0f;   // Sound 1 s vasthouden = geigerteller-demo

constexpr float STAT_INTERVAL_S = 5.0f;  // elke zoveel s een regel in de seriële log
