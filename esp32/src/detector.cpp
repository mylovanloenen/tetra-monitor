#include "detector.h"
#include <algorithm>
#include <math.h>

namespace {
float median(float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2];
}
float quantile(float* v, int n, float q) {
    int k = (int)(q * (n - 1) + 0.5f);
    std::nth_element(v, v + k, v + n);
    return v[k];
}
}

void Detector::process(const float* rssi, const int* segStart, const int* segLen, int nSeg,
                       float now, const bool* measured) {
    meas_ = measured;
    auto fresh = [&](int i) { return !measured || measured[i]; };
    float dt = lastT_ < 0 ? 0.0f : now - lastT_;
    dt_ = dt;
    lastT_ = now;
    ++sweeps_;

    // 1. Tijdmiddeling (voor de ruisschatting) + korte piek-hold (voor bursts).
    float decay = dt > 0 ? expf(-dt / PEAK_TAU_S) : 0.0f;
    for (int i = 0; i < N_CHANNELS; ++i) {
        Chan& c = ch_[i];
        if (sweeps_ == 1) { c.avg = rssi[i]; c.peak = rssi[i]; }
        if (!fresh(i)) continue;        // niet gemeten: niets bijwerken
        c.avg += CHAN_SMOOTH_A * (rssi[i] - c.avg);
        // piek zakt exponentieel terug naar de huidige meting
        c.peak = std::max(rssi[i], rssi[i] + (c.peak - rssi[i]) * decay);
    }

    // 2. Ruisvloer per kanaal → lev_[i] = hoeveel deze meting erboven uitsteekt.
    // Allebei uitrekenen (voor de vergelijkingslog); de gekozen telt voor de detectie.
    floorCfar(rssi, segStart, segLen, nSeg);
    for (int i = 0; i < N_CHANNELS; ++i) levC_[i] = lev_[i];
    float cfarFloor[N_CHANNELS];
    for (int i = 0; i < N_CHANNELS; ++i) cfarFloor[i] = floorOf_[i];
    floorPerChan(rssi, now);
    for (int i = 0; i < N_CHANNELS; ++i) levP_[i] = lev_[i];
    if (FLOOR_MODE == FLOOR_CFAR) {
        for (int i = 0; i < N_CHANNELS; ++i) { lev_[i] = levC_[i]; floorOf_[i] = cfarFloor[i]; }
    }
    if (sweeps_ > WARMUP_SWEEPS)
        for (int i = 0; i < N_CHANNELS; ++i) {
            if (!fresh(i)) continue;
            if (levC_[i] > cmpC_[i]) cmpC_[i] = levC_[i];
            if (levP_[i] > cmpP_[i]) cmpP_[i] = levP_[i];
        }
    float* floorOf = floorOf_;

    // 3. Waas: de héle ruisvloer tilt op als er een zender vlakbij staat.
    //    Normale vloer stijgt heel traag maar zakt direct mee.
    float tmp[N_CHANNELS];
    for (int i = 0; i < N_CHANNELS; ++i) tmp[i] = ch_[i].avg;
    floorNow_ = median(tmp, N_CHANNELS);
    if (sweeps_ <= WARMUP_SWEEPS || settle_ > 0) {
        if (settle_ > 0) --settle_;           // na een versterkingsstap: vloer opnieuw leren
        floorBase_ = floorNow_;
    } else if (floorNow_ < floorBase_) {
        floorBase_ = floorNow_;
    } else {
        floorBase_ = std::min(floorNow_, floorBase_ + FLOOR_BASE_UP_DB_S * dt);
    }
    bool warm = sweeps_ > WARMUP_SWEEPS;
    float maxRssi = -200;
    for (int i = 0; i < N_CHANNELS; ++i) maxRssi = std::max(maxRssi, rssi[i]);
    if (warm && (maxRssi > OVERLOAD_DBM || strong_))
        overloadUntil_ = now + OVERLOAD_HOLD_S;
    overload_ = now < overloadUntil_;
    // Waas: de vloer springt snel omhoog t.o.v. de meelopende normale vloer.
    if (warm && HAZE_ALARM && floorNow_ - floorBase_ > HAZE_RISE_DB)
        hazeUntil_ = now + OVERLOAD_HOLD_S;
    haze_ = now < hazeUntil_;

    // 4. Per kanaal: contact, blacklist en balk-ballistiek (attack/hold/release).
    float bestDb = 0;
    for (int i = 0; i < N_CHANNELS; ++i) {
        Chan& c = ch_[i];
        float level = !warm ? 0.0f : FLOOR_MODE == FLOOR_PERCHAN ? c.lpeak : c.peak - floorOf[i];
        // Herhaal-detectie op de losse meting van deze ronde (niet de piek-hold).
        const float repDb = FLOOR_MODE == FLOOR_PERCHAN ? REPEAT_DB_PERCHAN : REPEAT_DB;
        bool repeat = false;
        if (REPEAT_DETECT && warm && fresh(i) && lev_[i] >= repDb) {
            c.hits[c.hitPos] = now;
            c.hitPos = (c.hitPos + 1) % REPEAT_HITS;
            float oldest = c.hits[c.hitPos];             // oudste van de laatste REPEAT_HITS
            repeat = oldest > 0 && now - oldest <= REPEAT_WINDOW_S;
        }
        if (repeat && level <= soft_) { level = soft_ + 0.1f; ++repeatCount_; }
        bool contact = false;
        if (level > soft_) {
            if (c.activeSince == 0) c.activeSince = now;
            c.quietSince = 0;
            // Niet blacklisten zolang er een zender pal naast je staat (bv. een agent
            // met een actieve portofoon): dan moet hij juist blijven melden.
            if (!c.blacklisted && !strong_ && !overload_ && !haze_ && now - c.activeSince > BLACKLIST_S)
                c.blacklisted = true;
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
    } else if (bestDb >= soft_ || haze_) {
        alarm_ = 1; alarmDb_ = std::max(bestDb, soft_); alarmUntil_ = now + ALARM_TAIL_S;
    } else if (now >= alarmUntil_) {
        alarm_ = 0; alarmDb_ = 0;
    }   // anders: korte nahang, vorige waarde blijft staan
}

void Detector::snapshot(DetectorSnapshot& out) const {
    out.alarm = alarm_;
    out.alarmDb = alarmDb_;
    out.overload = overload_;
    out.haze = haze_;
    out.warm = sweeps_ > WARMUP_SWEEPS;
    for (int i = 0; i < N_CHANNELS; ++i) out.levels[i] = ch_[i].level;
}

// Oud: per ronde de stilste 20% van de buurkanalen (binnen één radio).
void Detector::floorCfar(const float* rssi, const int* segStart, const int* segLen, int nSeg) {
    float win[2 * CFAR_HALF_CHANS + 1];
    for (int s = 0; s < nSeg; ++s) {
        int a = segStart[s], n = segLen[s];
        for (int k = 0; k < n; ++k) {
            int m = 0;
            for (int j = k - CFAR_HALF_CHANS; j <= k + CFAR_HALF_CHANS; ++j) {
                int jj = j < 0 ? 0 : (j >= n ? n - 1 : j);   // rand: herhalen
                win[m++] = ch_[a + jj].avg;
            }
            floorOf_[a + k] = quantile(win, m, CFAR_QUANTILE);
        }
    }
    for (int i = 0; i < N_CHANNELS; ++i) lev_[i] = rssi[i] - floorOf_[i];
}

// Nieuw: elk kanaal leert z'n eigen ruis. Een portofoon zendt maar af en toe,
// dus de mediaan van een kanaal over de tijd = z'n ruis; een pulsje trekt die
// nauwelijks mee (max NF_STEP_DB per meting), een vaste storing wordt ruis.
void Detector::floorPerChan(const float* rssi, float now) {
    if (sweeps_ == 1) {
        learnUntil_ = now + NF_LEARN_S;
        for (int i = 0; i < N_CHANNELS; ++i) { ch_[i].nf = rssi[i]; ch_[i].dev = 3.0f; }
    }
    const float step = now < learnUntil_ ? NF_STEP_FAST_DB : NF_STEP_DB;

    // Vangnet: schuift de hele band tegelijk (oversturing, stad in), dan telt
    // die gezamenlijke verschuiving niet als signaal.
    float d[N_CHANNELS];
    for (int i = 0; i < N_CHANNELS; ++i) d[i] = rssi[i] - ch_[i].nf;
    float common = median(d, N_CHANNELS);

    float devs[N_CHANNELS];
    for (int i = 0; i < N_CHANNELS; ++i) devs[i] = ch_[i].dev;
    float devBand = median(devs, N_CHANNELS);
    devBand_ = devBand;

    float decay = 0;
    if (dt_ > 0) decay = expf(-dt_ / PEAK_TAU_S);
    for (int i = 0; i < N_CHANNELS; ++i) {
        Chan& c = ch_[i];
        float e = rssi[i] - c.nf - common;
        // mediaan-volger (alleen het eigen deel, niet de gezamenlijke verschuiving)
        float own = rssi[i] - c.nf;
        bool upd = !meas_ || meas_[i];
        if (upd) c.nf += own > 0 ? step : -step;
        // onrust = gemiddelde afwijking van de ruis; duidelijke pulsjes tellen niet mee
        float ae = fabsf(e);
        if (upd && ae < 4.0f * c.dev) c.dev += NF_DEV_A * (ae - c.dev);
        if (c.dev < NF_DEV_MIN) c.dev = NF_DEV_MIN;
        // schalen naar de onrust van de band: rustig kanaal gevoeliger, onrustig minder
        float k = devBand / c.dev;
        k = k < NF_NORM_MIN ? NF_NORM_MIN : k > NF_NORM_MAX ? NF_NORM_MAX : k;
        lev_[i] = e * k;
        c.lpeak = fmaxf(lev_[i], lev_[i] + (c.lpeak - lev_[i]) * decay);
        floorOf_[i] = c.nf + common;   // voor de log/compatibiliteit
    }
}
