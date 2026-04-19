#include "stats.h"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const uint16_t COLOR_GREEN  = rgb565( 76, 175,  80); // #4CAF50
static const uint16_t COLOR_MINT   = rgb565( 87, 217, 163); // #57D9A3
static const uint16_t COLOR_YELLOW = rgb565(255, 213,   0); // #FFD500
static const uint16_t COLOR_ORANGE = rgb565(255, 152,   0); // #FF9800
static const uint16_t COLOR_RED    = rgb565(244,  67,  54); // #F44336

Pace Stats::computePace(const UsageWindow& w, long totalSeconds, time_t now) {
    Pace p;
    if (!w.valid) return p;

    p.used = max(0.0, min(1.0, w.utilization / 100.0));

    if (w.resetsAt > 0 && now > 0 && totalSeconds > 0) {
        double remaining = (double)(w.resetsAt - now);
        if (remaining < 0) remaining = 0;
        if (remaining > (double)totalSeconds) remaining = totalSeconds;
        p.elapsed = 1.0 - remaining / (double)totalSeconds;
        if (p.elapsed < 0) p.elapsed = 0;
        if (p.elapsed > 1) p.elapsed = 1;
    }

    p.ratio = p.elapsed > 0.01 ? p.used / p.elapsed : 0;

    if (p.ratio < 0.75)       { p.label = "Well under pace"; p.color = COLOR_GREEN;  }
    else if (p.ratio < 0.95)  { p.label = "Under pace";      p.color = COLOR_MINT;   }
    else if (p.ratio < 1.10)  { p.label = "On pace";         p.color = COLOR_YELLOW; }
    else if (p.ratio < 1.35)  { p.label = "Over pace";       p.color = COLOR_ORANGE; }
    else                      { p.label = "Burning fast";    p.color = COLOR_RED;    }

    return p;
}

String Stats::formatCountdown(time_t resetsAt, time_t now, bool isWeekly) {
    if (resetsAt == 0 || now == 0) return "—";
    long remaining = (long)(resetsAt - now);
    if (remaining < 0) remaining = 0;

    if (isWeekly) {
        long days  = remaining / 86400;
        long hours = (remaining % 86400) / 3600;
        char buf[16];
        snprintf(buf, sizeof(buf), "%ldd %ldh", days, hours);
        return String(buf);
    } else {
        long hours = remaining / 3600;
        long mins  = (remaining % 3600) / 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%ldh %ldm", hours, mins);
        return String(buf);
    }
}
