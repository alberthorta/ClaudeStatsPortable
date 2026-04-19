#pragma once
#include <Arduino.h>
#include <time.h>

struct UsageWindow {
    bool   valid       = false;
    double utilization = 0;   // 0..100 from API
    time_t resetsAt    = 0;   // epoch seconds (UTC), 0 if unknown
};

struct Usage {
    bool        valid = false;
    UsageWindow fiveHour;
    UsageWindow sevenDay;
};

struct Pace {
    double      used    = 0;         // 0..1
    double      elapsed = 0;         // 0..1
    double      ratio   = 0;
    const char* label   = "";
    uint16_t    color   = 0xFFFF;    // RGB565
};

namespace Stats {
    Pace computePace(const UsageWindow& w, long totalSeconds, time_t now);

    // "2h 30m" for 5h; "3d 5h" for 7d
    String formatCountdown(time_t resetsAt, time_t now, bool isWeekly);
}
