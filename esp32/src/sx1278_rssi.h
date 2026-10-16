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
