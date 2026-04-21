#pragma once
#include <Arduino.h>
#include "stats.h"
#include "config.h"

namespace Display {
    void begin();

    void setRotation(int r);  // 0..3 — applies and recreates sprite
    int  rotation();

    // Toggle the backlight. Panel power and sprite stay live so waking up
    // is instant; the panel retains its last frame while dark.
    void setBacklight(bool on);

    // Send the ST7789 its SLPIN (0x10) / SLPOUT (0x11) commands so the panel
    // itself goes into low-power. Used around light-sleep. Safe to call while
    // the backlight is on; just don't expect a visible frame until SLPOUT.
    void panelSleep(bool in);

    // Generic "<title> in N…" full-screen countdown with a cancel hint.
    // Drawn direct to TFT. Force the backlight on so the message is visible
    // even if the screen had been turned off by tier 1. Title is the leading
    // phrase (e.g. "Deep sleep in", "Screen off in"). subtitle overrides the
    // default "any button cancels" line; nullptr keeps the default.
    void showSleepArmed(const char* title, int secondsLeft,
                         const char* subtitle = nullptr);

    // Brief "Sleeping…" frame shown for ~500 ms immediately before the
    // esp_deep_sleep_start() call so the user sees the transition.
    void showDeepSleep();

    void showProvisioning(const String& apSsid, const String& ip);
    void showConnecting(const String& ssid);
    void showResetting();

    void showLoading(const String& msg);
    void showApiError(const String& msg, int refreshInSec);

    // Full stats view: 5-hour panel + weekly panel.
    // batteryMv is the raw battery voltage in millivolts (for the icon).
    // refreshInSec is drawn to the left of the battery in landscape only
    // (portrait has no room for it).
    // stale=true draws a red "CACHED" badge next to the battery indicator.
    void showStats(const Usage& usage, time_t now, int batteryMv,
                   int refreshInSec, bool stale = false);

    void showInfo(const AppConfig& cfg, const String& ip, int batteryMv);
}
