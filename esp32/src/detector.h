// Detectie, overgezet uit tetra_monitor.py (Detector._detect):
// lokale ruis per kanaal (CFAR), piek-hold, balk-ballistiek, auto-blacklist,
// oversturing/waas. Invoer = één RSSI-waarde (dBm) per kanaal per ronde.
#pragma once
#include "config.h"

struct DetectorSnapshot {
    int   alarm;          // 0 = stil, 1 = oranje, 2 = rood
    float alarmDb;        // dB boven ruis van het sterkste contact
    bool  overload;       // zender pal naast je (zeer sterk)
    bool  haze;           // hele band springt omhoog (veel zenders tegelijk)
    bool  warm;           // ruisvloer opgebouwd
    float levels[N_CHANNELS];   // balkniveau per kanaal (dB boven ruis, na ballistiek)
};

class Detector {
public:
    void setThresholds(float soft, float hard) { soft_ = soft; hard_ = hard; }
    // rssi[] = dBm per kanaal; segStart/segLen = welk stuk door welke radio
    // gemeten is (CFAR blijft binnen één radio, want twee chips meten nooit
    // precies even hoog).
    // measured[i] = false: kanaal is deze ronde niet gemeten (focus-scan), rssi[i] is oud.
    void process(const float* rssi, const int* segStart, const int* segLen, int nSeg, float now,
                 const bool* measured = nullptr);
    void snapshot(DetectorSnapshot& out) const;
    float floorDbm() const { return floorNow_; }
    // Losse meting van de laatste ronde t.o.v. de ruis (dB), zonder piek-hold. Voor de waterval.
    float sweepExcessDb(int i) const { return sweeps_ > WARMUP_SWEEPS ? lev_[i] : 0.0f; }
    float noiseSpreadDb() const { return devBand_; }   // mediaan van de ruis-onrust per kanaal
    // Versterking is teruggeschakeld = er staat een zeer sterke zender vlakbij.
    void setStrongNearby(bool on) { strong_ = on; }
    // Na een versterkingsstap verschuift de ruisvloer: niet als waas tellen.
    void gainChanged() { settle_ = GAIN_SETTLE_SWEEPS; learnUntil_ = lastT_ + 1.0f; }
    // Piek t.o.v. de lokale ruis (dB), ook onder de drempel. Voor de meetlog.
    float excessDb(int i) const {
        if (sweeps_ <= WARMUP_SWEEPS) return 0.0f;
        return FLOOR_MODE == FLOOR_PERCHAN ? ch_[i].lpeak : ch_[i].peak - floorOf_[i];
    }
    // Aantal keer dat de herhaal-detectie een contact gaf (voor de log; wordt gereset).
    // Vergelijkingslog: hoogste losse meting per kanaal sinds de vorige keer,
    // in het oude (CFAR) en nieuwe (per kanaal) systeem. Daarna op 0.
    void takeCompare(float* c, float* p) {
        for (int i = 0; i < N_CHANNELS; ++i) { c[i] = cmpC_[i]; p[i] = cmpP_[i]; cmpC_[i] = cmpP_[i] = 0; }
    }
    uint32_t takeRepeatCount() { uint32_t n = repeatCount_; repeatCount_ = 0; return n; }

private:
    struct Chan {
        float avg = 0, peak = 0, level = 0;
        float hangUntil = 0, activeSince = 0, quietSince = 0;
        bool blacklisted = false;
        float nf = 0, dev = 3.0f, lpeak = 0;   // nieuw systeem: eigen ruis, onrust, piek
        float hits[REPEAT_HITS] = {};   // tijdstippen van de laatste meetpunten boven REPEAT_DB
        int hitPos = 0;
    };
    Chan ch_[N_CHANNELS];
    float floorOf_[N_CHANNELS] = {};
    float soft_ = MODES[0].soft, hard_ = MODES[0].hard;
    int sweeps_ = 0;
    float lastT_ = -1, dt_ = 0;
    float floorNow_ = 0, floorBase_ = 0;
    float overloadUntil_ = 0, alarmUntil_ = 0;
    int alarm_ = 0;
    float alarmDb_ = 0;
    bool overload_ = false;
    bool strong_ = false;
    bool haze_ = false;
    float hazeUntil_ = 0;
    int settle_ = 0;
    uint32_t repeatCount_ = 0;
    float learnUntil_ = 0;
    float devBand_ = 0;   // nieuw systeem: tot dan snel leren
    float lev_[N_CHANNELS] = {};
    float levC_[N_CHANNELS] = {}, levP_[N_CHANNELS] = {};
    float cmpC_[N_CHANNELS] = {}, cmpP_[N_CHANNELS] = {};   // excess van deze ronde (dB), beide systemen
    void floorCfar(const float* rssi, const int* segStart, const int* segLen, int nSeg);
    void floorPerChan(const float* rssi, float now);
    const bool* meas_ = nullptr;
};
