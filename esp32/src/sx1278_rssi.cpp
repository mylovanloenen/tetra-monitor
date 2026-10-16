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

constexpr uint8_t OPMODE_FSK_LF = 0x08;   // LongRangeMode=0 (FSK), LowFrequencyModeOn=1
constexpr double  FXOSC = 32e6;
constexpr double  FSTEP = FXOSC / 524288.0;   // 2^19 → 61,035 Hz per stap

const SPISettings kSpi(8000000, MSBFIRST, SPI_MODE0);
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

    write(REG_LNA, 0x23);           // vaste, maximale LNA-versterking + boost (handmatige gain)
    write(REG_RX_BW, 0x0D);         // Mant 20, Exp 5 → 12,5 kHz enkelzijdig = 25 kHz kanaal
    write(REG_RSSI_CONFIG, 0x00);   // geen offset, 2 samples middeling (snel)
    write(REG_RX_CONFIG, 0x00);     // AGC uit: zelfde gain op elk kanaal

    setMode(RX);
    delay(2);
    return true;
}

void Sx1278Rssi::tune(uint32_t hz) {
    uint32_t frf = (uint32_t)((double)hz / FSTEP + 0.5);
    write(REG_FRF_MSB, (frf >> 16) & 0xFF);
    write(REG_FRF_MID, (frf >> 8) & 0xFF);
    write(REG_FRF_LSB, frf & 0xFF);
    write(REG_RX_CONFIG, 0x40);     // RestartRxWithPllLock: opnieuw ontvangen op de nieuwe freq
}

float Sx1278Rssi::readRssiDbm() {
    return -0.5f * read(REG_RSSI_VALUE);
}
