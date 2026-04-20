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
