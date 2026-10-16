# PrioSense — ESP32-variant

Smart Panlee **SC01 Plus** (ESP32-S3 + 3,5"-scherm) met één of twee
**Ai-Thinker Ra-02** (SX1278) als RSSI-meter, in plaats van Pi 5 + RTL-SDR.

**Alleen ontvangen.** De firmware meet per 25 kHz-kanaal (380,9–384,1 MHz rond 382,5, C2000-uplink, zelfde venster als de Pi)
hoe sterk het signaal is. Er wordt niets gedecodeerd of opgeslagen, en de radio's
worden nooit in zendmodus gezet (`src/sx1278_rssi.cpp`).

## Aansluiten

EXT-connector (8-pins PicoBlade) → PicoBlade-naar-DuPont-kabel:

| EXT | Signaal | Naar |
|-----|---------|------|
| 1 | 5V | AMS1117 **VIN** |
| 2 | GND | AMS1117 **GND** + GND van beide Ra-02's |
| 3 | GPIO10 | Ra-02 **A**: NSS |
| 4 | GPIO11 | beide Ra-02's: MOSI |
| 5 | GPIO12 | beide Ra-02's: SCK |
| 6 | GPIO13 | beide Ra-02's: MISO |
| 7 | GPIO14 | Ra-02 **B**: NSS |
| 8 | GPIO21 | beide Ra-02's: RST |

AMS1117 **VOUT (3,3V)** → 3,3V van beide Ra-02's.
**Nooit 5V op een Ra-02.** Antenne via het SMA–uFL-kabeltje op de IPEX-aansluiting.

Speaker → SPK-connector (PicoBlade 2p).

Met maar één radio werkt het ook: sluit dan alleen radio A aan. Die scant dan alle 128 kanalen, vanuit 382,5 MHz om en om naar beneden en boven. Met twee radio's gaat A vanaf 382,5 naar beneden en B naar boven.

## Bouwen en flashen

```bash
pip install platformio
cd esp32
pio run -t upload
pio device monitor
```

De seriële log toont bij het opstarten of radio A en B gevonden zijn (versie `0x12`),
en daarna elke 5 s: rondes per seconde, ruisvloer en het sterkste kanaal.

## Bediening

Hetzelfde als op de Pi:

- **City / Highway:** de gevoeligheid.
- **Sound** kort tikken: geluid aan of uit.
- **Sound** 1 s vasthouden: geigerteller-test.
- **Lampje:** knippert groen zolang er gescand wordt, rood als het scannen stokt
  of als er geen radio is.

## Afregelen (bij de eerste test)

Alles staat in `src/config.h`.

1. **Scherm op z'n kop?** Zet `SCREEN_ROTATION` op 3 in plaats van 1.
2. **Ruisvloer:** zonder signaal zou de log rond −115 tot −125 dBm moeten geven.
3. **`DWELL_US`** (250): zo lang wacht de radio per kanaal. Te kort geeft een
   onrustige meting, te lang maakt het scannen trager.
4. **Drempels (`MODES`):** 35/45 en 25/35 dB komen van de SDR-versie. Een RSSI-chip
   meet anders, dus stel ze af aan de hand van de "sterkste"-regel in de log bij
   een echte melding.
5. **`OVERLOAD_DBM`** (−35): boven deze waarde gaat de meter vol rood
   (zender pal naast je).

## Simulator (Mac)

Dezelfde scherm- en detectiecode (`src/ui.cpp`, `src/detector.cpp`) in een venster,
met nagemaakte radiosignalen. Het scenario duurt 40 s en herhaalt zich. De muis is de touch.

```bash
brew install sdl2
cd esp32 && pio run          # één keer: haalt LovyanGFX op
sim/build.sh
sim/priosense_sim            # interactief
sim/priosense_sim --shots sim/out   # schermafbeeldingen van elke fase
```

Lettertypen aangepast? Draai dan `python3 tools/make_fonts.py`.
