// Detectie, overgezet uit tetra_monitor.py (Detector._detect):
// lokale ruis per kanaal (CFAR), piek-hold, balk-ballistiek, auto-blacklist,
// oversturing/waas. Invoer = één RSSI-waarde (dBm) per kanaal per ronde.
#pragma once
#include "config.h"

struct DetectorSnapshot {
    int   alarm;          // 0 = stil, 1 = oranje, 2 = rood
    float alarmDb;        // dB boven ruis van het sterkste contact
    bool  overload;       // oversturing of waas (zeer sterke zender vlakbij)
    bool  warm;           // ruisvloer opgebouwd
    float levels[N_CHANNELS];   // balkniveau per kanaal (dB boven ruis, na ballistiek)
};

class Detector {
public:
    void setThresholds(float soft, float hard) { soft_ = soft; hard_ = hard; }
    // rssi[] = dBm per kanaal; segStart/segLen = welk stuk door welke radio
    // gemeten is (CFAR blijft binnen één radio, want twee chips meten nooit
    // precies even hoog).
    void process(const float* rssi, const int* segStart, const int* segLen, int nSeg, float now);
    void snapshot(DetectorSnapshot& out) const;
    float floorDbm() const { return floorNow_; }

private:
    struct Chan {
        float avg = 0, peak = 0, level = 0;
        float hangUntil = 0, activeSince = 0, quietSince = 0;
        bool blacklisted = false;
    };
    Chan ch_[N_CHANNELS];
    float soft_ = MODES[0].soft, hard_ = MODES[0].hard;
    int sweeps_ = 0;
    float lastT_ = -1;
    float floorNow_ = 0, floorBase_ = 0;
    float overloadUntil_ = 0, alarmUntil_ = 0;
    int alarm_ = 0;
    float alarmDb_ = 0;
    bool overload_ = false;
};
