#include "detector.h"
#include <algorithm>
#include <math.h>

namespace {
float median(float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2];
}
}

void Detector::process(const float* rssi, const int* segStart, const int* segLen, int nSeg,
                       float now) {
    float dt = lastT_ < 0 ? 0.0f : now - lastT_;
    lastT_ = now;
    ++sweeps_;

    // 1. Tijdmiddeling (voor de ruisschatting) + korte piek-hold (voor bursts).
    float decay = dt > 0 ? expf(-dt / PEAK_TAU_S) : 0.0f;
    for (int i = 0; i < N_CHANNELS; ++i) {
        Chan& c = ch_[i];
        if (sweeps_ == 1) { c.avg = rssi[i]; c.peak = rssi[i]; }
        c.avg += CHAN_SMOOTH_A * (rssi[i] - c.avg);
        // piek zakt exponentieel terug naar de huidige meting
        c.peak = std::max(rssi[i], rssi[i] + (c.peak - rssi[i]) * decay);
    }

    // 2. CFAR: lokale ruis = mediaan van de buren, binnen het stuk van één radio.
    static float floorOf[N_CHANNELS];
    float win[2 * CFAR_HALF_CHANS + 1];
    for (int s = 0; s < nSeg; ++s) {
        int a = segStart[s], n = segLen[s];
        for (int k = 0; k < n; ++k) {
            int m = 0;
            for (int j = k - CFAR_HALF_CHANS; j <= k + CFAR_HALF_CHANS; ++j) {
                int jj = j < 0 ? 0 : (j >= n ? n - 1 : j);   // rand: herhalen
                win[m++] = ch_[a + jj].avg;
            }
            floorOf[a + k] = median(win, m);
        }
    }

    // 3. Waas: de héle ruisvloer tilt op als er een zender vlakbij staat.
    //    Normale vloer stijgt heel traag maar zakt direct mee.
    float tmp[N_CHANNELS];
    for (int i = 0; i < N_CHANNELS; ++i) tmp[i] = ch_[i].avg;
    floorNow_ = median(tmp, N_CHANNELS);
    if (sweeps_ <= WARMUP_SWEEPS) {
        floorBase_ = floorNow_;
    } else if (floorNow_ < floorBase_) {
        floorBase_ = floorNow_;
    } else {
        floorBase_ = std::min(floorNow_, floorBase_ + FLOOR_BASE_UP_DB_S * dt);
    }
    bool warm = sweeps_ > WARMUP_SWEEPS;
    float maxRssi = -200;
    for (int i = 0; i < N_CHANNELS; ++i) maxRssi = std::max(maxRssi, rssi[i]);
    if (warm && (floorNow_ - floorBase_ > HAZE_RISE_DB || maxRssi > OVERLOAD_DBM))
        overloadUntil_ = now + OVERLOAD_HOLD_S;
    overload_ = now < overloadUntil_;

    // 4. Per kanaal: contact, blacklist en balk-ballistiek (attack/hold/release).
    float bestDb = 0;
    for (int i = 0; i < N_CHANNELS; ++i) {
        Chan& c = ch_[i];
        float level = warm ? c.peak - floorOf[i] : 0.0f;
        bool contact = false;
        if (level > soft_) {
            if (c.activeSince == 0) c.activeSince = now;
            c.quietSince = 0;
            if (!c.blacklisted && now - c.activeSince > BLACKLIST_S) c.blacklisted = true;
            contact = !c.blacklisted;
        } else {
            c.activeSince = 0;
            if (c.quietSince == 0) c.quietSince = now;
            if (c.blacklisted && now - c.quietSince > UNBLACKLIST_S) c.blacklisted = false;
        }

        if (contact) {
            if (level > c.level) c.level = level;
            c.hangUntil = now + HANG_TIME_S;
            bestDb = std::max(bestDb, level);
        } else if (c.blacklisted) {
            c.level = 0;
        } else if (now > c.hangUntil && c.level > 0) {
            c.level = std::max(0.0f, c.level - RELEASE_DB_S * dt);
        }
    }

    // 5. Alarmniveau. Oversturing/waas = zeer sterk dichtbij → juist rood.
    if (overload_) {
        alarm_ = 2; alarmDb_ = std::max(bestDb, hard_); alarmUntil_ = now + ALARM_TAIL_S;
    } else if (bestDb >= hard_) {
        alarm_ = 2; alarmDb_ = bestDb; alarmUntil_ = now + ALARM_TAIL_S;
    } else if (bestDb >= soft_) {
        alarm_ = 1; alarmDb_ = bestDb; alarmUntil_ = now + ALARM_TAIL_S;
    } else if (now >= alarmUntil_) {
        alarm_ = 0; alarmDb_ = 0;
    }   // anders: korte nahang, vorige waarde blijft staan
}

void Detector::snapshot(DetectorSnapshot& out) const {
    out.alarm = alarm_;
    out.alarmDb = alarmDb_;
    out.overload = overload_;
    out.warm = sweeps_ > WARMUP_SWEEPS;
    for (int i = 0; i < N_CHANNELS; ++i) out.levels[i] = ch_[i].level;
}
