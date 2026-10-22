#include "sx1278_rssi.h"

namespace {
// Registers (SX1276/77/78/79 datasheet, FSK/OOK-pagina)
constexpr uint8_t REG_OP_MODE     = 0x01;
constexpr uint8_t REG_FRF_MSB     = 0x06;
constexpr uint8_t REG_FRF_MID     = 0x07;
constexpr uint8_t REG_FRF_LSB     = 0x08;
constexpr uint8_t REG_LNA         = 0x0C;
constexpr uint8_t REG_RX_CONFIG   = 0x0D;
constexpr uint8_t REG_RSSI_CONFIG = 0x0E;
constexpr uint8_t REG_RSSI_VALUE  = 0x11;
constexpr uint8_t REG_RX_BW       = 0x12;
constexpr uint8_t REG_IMAGE_CAL   = 0x3B;
constexpr uint8_t REG_VERSION     = 0x42;
constexpr uint8_t REG_PLL_HOP     = 0x44;   // bit 7 = FastHopOn

constexpr uint8_t OPMODE_FSK_LF = 0x08;   // LongRangeMode=0 (FSK), LowFrequencyModeOn=1
constexpr double  FXOSC = 32e6;
constexpr double  FSTEP = FXOSC / 524288.0;   // 2^19 → 61,035 Hz per stap

const SPISettings kSpi(10000000, MSBFIRST, SPI_MODE0);   // max. van de SX1278
}

uint8_t Sx1278Rssi::read(uint8_t reg) {
    spi_.beginTransaction(kSpi);
    digitalWrite(nss_, LOW);
    spi_.transfer(reg & 0x7F);
    uint8_t v = spi_.transfer(0);
    digitalWrite(nss_, HIGH);
    spi_.endTransaction();
    return v;
}

void Sx1278Rssi::write(uint8_t reg, uint8_t val) {
    spi_.beginTransaction(kSpi);
    digitalWrite(nss_, LOW);
    spi_.transfer(reg | 0x80);
    spi_.transfer(val);
    digitalWrite(nss_, HIGH);
    spi_.endTransaction();
}

void Sx1278Rssi::setMode(Mode m) {
    // Alleen slapen/standby/ontvangen; zenden (TX/FSTX) kan hier niet.
    if (m != SLEEP && m != STANDBY && m != RX) return;
    write(REG_OP_MODE, OPMODE_FSK_LF | m);
}

bool Sx1278Rssi::begin(uint32_t centerHz) {
    pinMode(nss_, OUTPUT);
    digitalWrite(nss_, HIGH);

    version_ = read(REG_VERSION);
    if (version_ != 0x12) return false;        // SX1276/77/78 meldt 0x12

    // Van LoRa naar FSK mag alleen in slaapstand.
    write(REG_OP_MODE, 0x00);                   // slapen (welke modem dan ook)
    delay(2);
    setMode(SLEEP);
    delay(2);
    setMode(STANDBY);
    delay(2);

    tune(centerHz);
    // Beeldonderdrukking kalibreren op deze band (standaard staat hij op 434 MHz).
    write(REG_IMAGE_CAL, read(REG_IMAGE_CAL) | 0x40);
    for (int i = 0; i < 50 && (read(REG_IMAGE_CAL) & 0x20); ++i) delay(1);

    setLnaGain(1);                  // handmatige gain, start op maximaal (G1)
    write(REG_RX_BW, 0x0D);         // Mant 20, Exp 5 → 12,5 kHz enkelzijdig = 25 kHz kanaal
    write(REG_RSSI_CONFIG, 0x00);   // geen offset, 2 samples middeling (snel)
    write(REG_RX_CONFIG, 0x00);     // AGC uit: zelfde gain op elk kanaal

    write(REG_PLL_HOP, read(REG_PLL_HOP) | 0x80);   // FastHopOn: snel van kanaal wisselen in RX
    setMode(RX);
    delay(2);
    return true;
}

void Sx1278Rssi::tune(uint32_t hz) {
    uint32_t frf = (uint32_t)((double)hz / FSTEP + 0.5);
    // Alle drie FRF-registers in één SPI-bericht (adres loopt vanzelf op);
    // het schrijven van FRF_LSB start de sprong.
    spi_.beginTransaction(kSpi);
    digitalWrite(nss_, LOW);
    spi_.transfer(REG_FRF_MSB | 0x80);
    spi_.transfer((frf >> 16) & 0xFF);
    spi_.transfer((frf >> 8) & 0xFF);
    spi_.transfer(frf & 0xFF);
    digitalWrite(nss_, HIGH);
    spi_.endTransaction();
    // FastHop: de ontvanger blijft aan en verspringt alleen van frequentie
    // (~20 µs, het schrijven van FRF_LSB start de sprong). Een herstart van de
    // ontvanger kost bij deze filterbreedte ~0,5 ms (datablad, TS_RE 504 µs):
    // dan las je steeds de meting van een paar kanalen eerder af.
}

void Sx1278Rssi::setLnaGain(int step) {
    step = step < 1 ? 1 : step > 6 ? 6 : step;
    write(REG_LNA, (uint8_t)(step << 5));   // LnaGain in bit 7-5; boost-bits 0 (LF-ingang)
}

float Sx1278Rssi::readRssiDbm() {
    return -0.5f * read(REG_RSSI_VALUE);
}

uint32_t Sx1278Rssi::frfFor(uint32_t hz) { return (uint32_t)((double)hz / FSTEP + 0.5); }

void Sx1278Rssi::beginBurst() { spi_.beginTransaction(kSpi); }
void Sx1278Rssi::endBurst() { spi_.endTransaction(); }

void Sx1278Rssi::tuneFast(uint32_t frf) {
    uint8_t b[4] = {(uint8_t)(REG_FRF_MSB | 0x80), (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)frf};
    digitalWrite(nss_, LOW);
    spi_.writeBytes(b, 4);          // FRF_LSB als laatste: start de FastHop-sprong
    digitalWrite(nss_, HIGH);
}

float Sx1278Rssi::readRssiFast() {
    uint8_t out[2] = {REG_RSSI_VALUE & 0x7F, 0}, in[2];
    digitalWrite(nss_, LOW);
    spi_.transferBytes(out, in, 2);
    digitalWrite(nss_, HIGH);
    return -0.5f * in[1];
}
