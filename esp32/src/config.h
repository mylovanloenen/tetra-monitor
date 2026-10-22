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
// TEST_433 = true: tijdelijk scannen rond 433,92 MHz om de hele keten thuis te
// testen met een autosleutel (die zendt daar). Daarna wéér op false zetten!
constexpr bool     TEST_433      = false;
// TEST_DOWNLINK = true: tijdelijk de C2000-downlink (390,9–394,1 MHz) scannen,
// om te zien hoe sterk de basisstations zijn (bv. bij Schiphol). Alleen meten.
// Daarna wéér op false zetten!
constexpr bool     TEST_DOWNLINK = false;
constexpr uint32_t CENTER_HZ     = TEST_433 ? 433920000 : TEST_DOWNLINK ? 392500000 : 382500000;
constexpr uint32_t CHANNEL_HZ    = 25000;
constexpr int      N_CHANNELS    = 128;
constexpr int      CENTER_IDX    = N_CHANNELS / 2;    // kanaal 64 = 382,500 MHz
constexpr uint32_t BAND_START_HZ = CENTER_HZ - CENTER_IDX * CHANNEL_HZ;   // 380,900 MHz

// Hoe lang de radio op een kanaal blijft voor de RSSI-meting (µs). Korter =
// sneller rond, maar de meting moet wel stabiel zijn: bij binnenkomst van de
// onderdelen afregelen (zie README, "Afregelen").
// Eigen stoorpiek ("birdie") van de Ra-02: 12 × 32 MHz-kristal = 384,000 MHz.
// Kanalen binnen ±SPUR_HALF_HZ hiervan tellen niet mee (detectie én gain).
constexpr uint32_t SPUR_HZ[]     = {384000000};
constexpr uint32_t SPUR_HALF_HZ  = 100000;   // ±100 kHz: 383,900 lekte nog mee (stond altijd op +17)
// Onderrand van de band: 380,900–380,950 stonden in rust altijd op +16…+19 dB
// (rand van het filter / minder buren voor de ruisschatting) → negeren.
constexpr uint32_t EDGE_LOW_MASK_HZ = 380975000;   // kanalen hieronder tellen niet mee
// RSSI-middeling in de chip: 2, 4, 8 of 16 samples. Elke verdubbeling ≈ 40 µs
// extra meettijd per kanaal (bij 12,5 kHz filter), maar minder ruis per meting.
// Focus-scan: kanalen waar net iets gezien is elke ronde meten, de rest van de
// band om de ronde (helft A / helft B). Zo volgt hij een zender snel en blijft
// hij toch de hele band afzoeken.
constexpr bool  FOCUS_SCAN   = true;
constexpr float FOCUS_DB     = 12.0f;   // kanaal (en z'n buren) wordt "heet" boven dit (dB)
constexpr float FOCUS_HOLD_S = 3.0f;    // zo lang blijft een kanaal heet na het laatste contact
constexpr int   FOCUS_MAX    = 24;      // hooguit zoveel hete kanalen (de sterkste)
constexpr int RSSI_SAMPLES = 8;   // bureautest: 8 samples = 30% minder ruis-onrust (2,9 → 2,0 dB)
constexpr uint32_t DWELL_US = RSSI_SAMPLES <= 2 ? 75 : RSSI_SAMPLES == 4 ? 115 : RSSI_SAMPLES == 8 ? 195 : 355;
// per kanaal: fast hop ~20 µs + RSSI-meting ~40 µs (datablad)

// ── Detectie (zelfde opzet als tetra_monitor.py) ────────────────────────────
// LET OP: de dB-drempels komen van de Pi/SDR-versie. Een RSSI-chip meet
// anders, dus deze getallen moeten bij de eerste testrit opnieuw afgesteld
// worden (README, "Afregelen").
// Ruisvloer-systeem:
//  FLOOR_CFAR    = oud: per ronde de stilste 20% van ±12 buurkanalen.
//  FLOOR_PERCHAN = nieuw: elk kanaal leert z'n eigen ruis over de tijd (mediaan)
//                  plus hoe onrustig die ruis is; de hele band als vangnet.
enum FloorMode { FLOOR_CFAR, FLOOR_PERCHAN };
constexpr FloorMode FLOOR_MODE = FLOOR_CFAR;   // bureautest: PERCHAN gaf geen winst (ruispieken even hoog)

struct DriveMode { const char* name; float soft; float hard; };
// Drempels voor het nieuwe systeem (dB boven de eigen ruis van het kanaal,
// geschaald naar de gemiddelde onrust van de band). Afgesteld op het bureau.
constexpr DriveMode MODES_PERCHAN[] = {
    {"City",    18.0f, 26.0f},
    {"Highway", 14.0f, 21.0f},
};
constexpr DriveMode MODES_CFAR[] = {
    {"City",    31.0f, 39.0f},   // Stad (op verzoek +5 t.o.v. 26/34)
    {"Highway", 25.0f, 30.0f},   // Snelweg (op verzoek, was 17/24)
};
constexpr const DriveMode* MODES = FLOOR_MODE == FLOOR_PERCHAN ? MODES_PERCHAN : MODES_CFAR;
constexpr int N_MODES = 2;

// Nieuw ruissysteem (FLOOR_PERCHAN)
constexpr float NF_STEP_DB      = 0.03f;  // mediaan-volger: zoveel dB per meting omhoog/omlaag
constexpr float NF_STEP_FAST_DB = 0.15f;  // … de eerste NF_LEARN_S na het opstarten / een gain-stap
constexpr float NF_LEARN_S      = 3.0f;
constexpr float NF_DEV_A        = 0.02f;  // middeling van de onrust (gem. afwijking) per kanaal
constexpr float NF_DEV_MIN      = 1.0f;   // onrust nooit lager dan dit (dB)
constexpr float NF_NORM_MIN     = 0.5f;   // onrustig kanaal telt maximaal 2× zwakker…
constexpr float NF_NORM_MAX     = 1.5f;   // …rustig kanaal maximaal 1,5× sterker
constexpr float REPEAT_DB_PERCHAN = 10.0f; // herhaal-drempel in het nieuwe systeem

constexpr int   CFAR_HALF_CHANS   = 12;     // lokale ruis uit ±12 buren…
constexpr float CFAR_QUANTILE     = 0.20f;  // …de stilste 20% (niet de mediaan): bij veel zenders
                                            // tegelijk (bureau) blijft de ruisschatting dan laag
// Herhaal-detectie: een portofoon zendt steeds opnieuw op hetzelfde kanaal
// (TETRA: elke ~57 ms een pulsje), ruis piekt willekeurig. Komt één kanaal
// binnen REPEAT_WINDOW_S minstens REPEAT_HITS keer REPEAT_DB boven de ruis,
// dan is dat een zender → oranje, ook ver onder de gewone drempel.
constexpr bool  REPEAT_DETECT     = true;
constexpr float REPEAT_DB         = 18.0f;   // vast, in beide rijstanden (meeschuiven met oranje: teruggedraaid)
constexpr int   REPEAT_HITS       = 3;
constexpr float REPEAT_WINDOW_S   = 0.30f;
constexpr float CHAN_SMOOTH_A     = 0.20f;  // tijdmiddeling per kanaal
constexpr float PEAK_TAU_S        = 0.3f;   // piek-hold van korte bursts
constexpr float HANG_TIME_S       = 4.0f;   // balk blijft staan na laatste contact
constexpr float RELEASE_DB_S      = 12.0f;  // daarna zakt de balk zo snel
constexpr float BLACKLIST_S       = 20.0f;  // zo lang continu actief = storing
constexpr float UNBLACKLIST_S     = 15.0f;  // zo lang stil = weer meedoen
constexpr bool  HAZE_ALARM        = true;   // waas: hele vloer springt snel omhoog → oranje
constexpr float HAZE_RISE_DB      = 8.0f;   // zoveel dB boven de meelopende "normale" vloer…
                                            // (stad inrijden gaat langzaam: loopt mee, geen alarm;
                                            //  veel zenders tegelijk springt in seconden: wel alarm)
constexpr float OVERLOAD_DBM      = -35.0f; // RSSI hierboven = zender pal naast je

// Automatische versterking (LNA-stand in de SX1278, zoals de gain-stap op de
// Pi). Pal naast een sterke zender raakt de chip anders verzadigd: dan gaan
// alle kanalen tegelijk omhoog, steekt er niets meer uit en gaf hij niks.
// Stand 1 = maximaal (G1), elke stand lager = minder versterking (zie datablad:
// G2 −6, G3 −12, G4 −24, G5 −36 dB). De RSSI blijft in dBm kloppen.
constexpr bool  AUTO_GAIN         = true;   // false = altijd maximale versterking (zoals eerst)
constexpr float VERY_STRONG_DBM   = -20.0f; // alleen voor de log: telt metingen hierboven
constexpr float GAIN_DOWN_DBM     = -55.0f; // sterkste kanaal hierboven → stand omlaag
constexpr float GAIN_UP_DBM       = -75.0f; // alles hieronder…
constexpr float GAIN_UP_AFTER_S   = 2.0f;   // …zo lang → in één keer terug naar G1
constexpr float GAIN_STEP_MIN_S   = 0.5f;   // minimaal zo lang tussen twee stappen
constexpr int   GAIN_STEP_MAX     = 5;      // laagste stand die we gebruiken (G5)
constexpr int   GAIN_SETTLE_SWEEPS = 15;    // na een stap even geen waas-detectie
constexpr float OVERLOAD_HOLD_S   = 5.0f;   // oversturing/waas zo lang vasthouden
constexpr float FLOOR_BASE_UP_DB_S = 0.5f;  // normale vloer loopt zo snel mee omhoog (dB/s)
constexpr float ALARM_TAIL_S      = 2.0f;   // korte nahang na de laatste detectie
constexpr int   WARMUP_SWEEPS     = 40;     // rondes om de ruisvloer op te bouwen

// ── Geluid ──────────────────────────────────────────────────────────────────
enum BeepSound { SOUND_BLIP, SOUND_BARK, SOUND_PING };
constexpr BeepSound BEEP_SOUND = SOUND_PING;   // blip / hond "waf" (bark.h) / internet-ping (ping.h)
constexpr float BEEP_PULSE_S   = 0.08f;  // duur van één "blip"
// Tempo (tijd van ping tot ping): oranje rustig, rood sneller dan oranje maar
// niet ratelend. Binnen elke kleur iets sneller naarmate het signaal sterker is.
constexpr float BEEP_ORANGE_SLOW_S = 1.10f;  // net boven oranje
constexpr float BEEP_ORANGE_FAST_S = 0.80f;  // net onder rood
constexpr float BEEP_RED_SLOW_S    = 0.55f;  // net boven rood
constexpr float BEEP_RED_FAST_S    = 0.38f;  // bij BEEP_TOP_DB en hoger
constexpr float BEEP_MAX_GAP_S = BEEP_ORANGE_SLOW_S;  // (geluidstest: van traag…
constexpr float BEEP_MIN_GAP_S = BEEP_RED_FAST_S;     // …naar snel)
constexpr float BEEP_TOP_DB    = 50.0f;  // hier is het tempo maximaal
constexpr int   BEEP_F_START   = 1300;   // blip glijdt omhoog van…
constexpr int   BEEP_F_END     = 1900;   // …naar deze toon (Hz)
// Volume: tikken op Sound gaat rond Uit → Zacht → Middel → Hard → Uit.
constexpr float VOLUME_LEVELS[] = {0.0f, 0.35f, 0.65f, 1.0f};
constexpr int   N_VOLUME        = 4;
constexpr int   DEFAULT_VOLUME  = 3;     // Hard
constexpr float SOUND_TEST_S   = 4.0f;   // Sound 1 s vasthouden = geigerteller-demo

constexpr float STAT_INTERVAL_S = 1.0f;  // elke zoveel s een [scan]-regel in de seriële log
constexpr uint32_t TOP_INTERVAL_MS = 200;
constexpr bool  LOG_FLOOR_CMP   = true;   // [cmp]-regels: oud vs nieuw ruissysteem op dezelfde metingen   // elke zoveel ms een [top]-regel (5× per seconde)

// SENS_MAX = true: tijdelijk maximaal gevoelig voor een meetrit. Drempels
// worden 10/20 dB (oranje/rood) in elke modus, en elke seconde komt er een
// [top]-regel met de 5 kanalen die het hoogst boven de ruis uitkomen.
// Daarna weer op false zetten!
constexpr bool  SENS_MAX       = false;
constexpr bool  LOG_TOP        = true;   // elke seconde [top]-regel, los van SENS_MAX
constexpr float SENS_MAX_SOFT  = 10.0f;
constexpr float SENS_MAX_HARD  = 20.0f;
