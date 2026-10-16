// Minimale Arduino-vervanger voor de simulator op de Mac.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
using std::max;
using std::min;
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295f
#endif
inline uint32_t millis() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
