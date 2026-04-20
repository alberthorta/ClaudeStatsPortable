#include "battery.h"
#include <Arduino.h>

static const int PIN = 4;

int Battery::millivolts() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += analogReadMilliVolts(PIN);
    }
    // x2 compensates the 2:1 resistor divider on the T-Display S3.
    return (int)((sum * 2) / 8);
}

int Battery::percent(int mv) {
    struct Point { int mv; int pct; };
    static const Point curve[] = {
        {4200, 100}, {4100, 92}, {4000, 85}, {3900, 70},
        {3800, 55},  {3700, 40}, {3600, 25}, {3500, 10},
        {3300, 0}
    };
    const size_t N = sizeof(curve) / sizeof(curve[0]);
    if (mv >= curve[0].mv) return 100;
    for (size_t i = 1; i < N; i++) {
        if (mv >= curve[i].mv) {
            int dmv  = curve[i-1].mv  - curve[i].mv;
            int dpct = curve[i-1].pct - curve[i].pct;
            return curve[i].pct + (mv - curve[i].mv) * dpct / dmv;
        }
    }
    return 0;
}

bool Battery::isCharging(int mv) {
    return mv >= 4150;
}
