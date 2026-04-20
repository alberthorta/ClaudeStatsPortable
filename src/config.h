#pragma once
#include <Arduino.h>

struct AppConfig {
    String ssid;
    String password;
    String sessionKey;
    String orgId;

    // Daily auto-open of a 5h window. Stored in UTC; the web form edits in
    // local time and converts via the browser's offset. offsetMin is kept only
    // so the device can display the schedule in local time on the Info screen.
    bool   autoOpenEnabled   = false;
    int    autoOpenHourUtc   = 9;   // 0..23
    int    autoOpenMinuteUtc = 0;   // 0..59
    int    autoOpenOffsetMin = 0;   // signed minutes east of UTC (display only)

    bool isValid() const {
        return ssid.length() > 0 && sessionKey.length() > 0;
    }
};

namespace Config {
    bool load(AppConfig& out);
    bool save(const AppConfig& cfg);
    void clear();
    void clearWifi();

    int  loadRotation(int fallback = 1);
    void saveRotation(int r);

    // YYYYMMDD in UTC of the last successful auto-open; 0 if never.
    uint32_t loadLastAutoOpenDate();
    void     saveLastAutoOpenDate(uint32_t yyyymmdd);
}
