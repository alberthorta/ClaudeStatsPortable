#pragma once
#include <Arduino.h>

static const int MAX_WIFIS = 8;

struct WifiCred {
    String ssid;
    String password;
};

struct AppConfig {
    String ssid;        // last-successfully-connected SSID (cached for display + migration)
    String password;
    String sessionKey;
    String orgId;

    // Known WiFi credentials. On boot the device scans the air, cross-checks
    // against this list, and connects to the strongest match. Editable via
    // the admin UI.
    WifiCred wifis[MAX_WIFIS];
    int      wifiCount = 0;

    // Daily auto-open of a 5h window. Stored in UTC; the web form edits in
    // local time and converts via the browser's offset. offsetMin is kept only
    // so the device can display the schedule in local time on the Info screen.
    bool   autoOpenEnabled   = false;
    int    autoOpenHourUtc   = 9;   // 0..23
    int    autoOpenMinuteUtc = 0;   // 0..59
    int    autoOpenOffsetMin = 0;   // signed minutes east of UTC (display only)

    // Tier 2 — minutes of idle (backlight off, no button) before entering
    // light sleep. 0 disables auto light sleep entirely.
    int    idleSleepMin      = 5;

    // HTTP Basic auth credentials for the admin web UI. Default is admin/admin;
    // a factory reset (Config::clear) wipes NVS so these fall back to defaults.
    String adminUser         = "admin";
    String adminPassword     = "admin";

    bool isValid() const {
        return (wifiCount > 0 || ssid.length() > 0) && sessionKey.length() > 0;
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

    // Find index of a saved SSID, or -1.
    int  findWifi(const AppConfig& cfg, const String& ssid);
    // Add or update a saved WiFi. Returns false only when inserting a new
    // entry and the list is already full.
    bool addOrUpdateWifi(AppConfig& cfg, const String& ssid, const String& password);
    // Remove by index. Shifts remaining entries up. Returns false on bad idx.
    bool removeWifiAt(AppConfig& cfg, int index);
    // Swap the entry at `index` with its neighbour `index + delta`. delta is
    // expected to be +1 or -1. Returns false if the move would go out of
    // bounds. The list order is the connection priority (lower index = higher
    // priority), so these are the arrows shown in the admin UI.
    bool moveWifi(AppConfig& cfg, int index, int delta);
}
