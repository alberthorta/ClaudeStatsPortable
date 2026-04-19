#pragma once
#include <Arduino.h>
#include "stats.h"
#include "config.h"

namespace Display {
    void begin();

    void showProvisioning(const String& apSsid, const String& ip);
    void showConnecting(const String& ssid);
    void showResetting();

    void showLoading(const String& msg);
    void showApiError(const String& msg, int refreshInSec);

    // Full stats view: 5-hour panel + weekly panel.
    // refreshInSec is the countdown until the next API fetch.
    void showStats(const Usage& usage, time_t now, int refreshInSec);

    void showInfo(const AppConfig& cfg, const String& ip);
}
