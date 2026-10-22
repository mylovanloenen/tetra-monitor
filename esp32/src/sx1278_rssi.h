// SX1278 (Ai-Thinker Ra-02) als pure RSSI-meter.
//
// ALLEEN ONTVANGEN: deze driver kent maar drie standen — slapen, standby en
// ontvangen. Zendregisters (PA, FIFO-schrijven) worden nooit aangeraakt en
// setMode() weigert elke andere stand. Er wordt niets gedemoduleerd of
// gedecodeerd: per kanaal lezen we alleen het RSSI-register (signaalsterkte).
#pragma once
#include <Arduino.h>
#include <SPI.h>

class Sx1278Rssi {
public:
    Sx1278Rssi(SPIClass& spi, int pinNss) : spi_(spi), nss_(pinNss) {}

    // Eén keer na de gezamenlijke reset. false = geen (werkende) chip gevonden.
    bool begin(uint32_t centerHz);

    // Afstemmen + RX herstarten; daarna DWELL_US wachten en readRssiDbm().
    void tune(uint32_t hz);
    float readRssiDbm();
    // LNA-stand 1 (G1, maximaal) … 6 (G6, −48 dB). Geldt vanaf de volgende tune().
    void setLnaGain(int step);

    // Snelle scan: één SPI-transactie per ronde (beginBurst … endBurst) en een
    // vooraf berekende FRF-waarde per kanaal. Tussen begin/end alleen de *Fast-functies.
    static uint32_t frfFor(uint32_t hz);
    void beginBurst();
    void endBurst();
    void tuneFast(uint32_t frf);
    float readRssiFast();

    uint8_t version() const { return version_; }

private:
    enum Mode : uint8_t { SLEEP = 0x00, STANDBY = 0x01, RX = 0x05 };
    void setMode(Mode m);
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t val);

    SPIClass& spi_;
    int nss_;
    uint8_t version_ = 0;
};
