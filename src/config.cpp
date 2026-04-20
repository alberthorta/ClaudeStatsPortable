#include "config.h"
#include <Preferences.h>

static const char* NS = "claudestats";

bool Config::load(AppConfig& out) {
    Preferences prefs;
    if (!prefs.begin(NS, true)) return false;
    out.ssid = prefs.getString("ssid", "");
    out.password = prefs.getString("password", "");
    out.sessionKey = prefs.getString("sessionKey", "");
    out.orgId = prefs.getString("orgId", "");
    out.autoOpenEnabled   = prefs.getBool ("aoEn",  false);
    out.autoOpenHourUtc   = prefs.getInt  ("aoHour", 9);
    out.autoOpenMinuteUtc = prefs.getInt  ("aoMin",  0);
    out.autoOpenOffsetMin = prefs.getInt  ("aoOff",  0);
    prefs.end();
    return out.isValid();
}

bool Config::save(const AppConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NS, false)) return false;
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("password", cfg.password);
    prefs.putString("sessionKey", cfg.sessionKey);
    prefs.putString("orgId", cfg.orgId);
    prefs.putBool ("aoEn",  cfg.autoOpenEnabled);
    prefs.putInt  ("aoHour", cfg.autoOpenHourUtc);
    prefs.putInt  ("aoMin",  cfg.autoOpenMinuteUtc);
    prefs.putInt  ("aoOff",  cfg.autoOpenOffsetMin);
    prefs.end();
    return true;
}

void Config::clear() {
    Preferences prefs;
    if (prefs.begin(NS, false)) {
        prefs.clear();
        prefs.end();
    }
}

void Config::clearWifi() {
    Preferences prefs;
    if (prefs.begin(NS, false)) {
        prefs.remove("ssid");
        prefs.remove("password");
        prefs.end();
    }
}

int Config::loadRotation(int fallback) {
    Preferences prefs;
    if (!prefs.begin(NS, true)) return fallback;
    int r = prefs.getInt("rotation", fallback);
    prefs.end();
    return r;
}

void Config::saveRotation(int r) {
    Preferences prefs;
    if (prefs.begin(NS, false)) {
        prefs.putInt("rotation", r);
        prefs.end();
    }
}

uint32_t Config::loadLastAutoOpenDate() {
    Preferences prefs;
    if (!prefs.begin(NS, true)) return 0;
    uint32_t v = prefs.getUInt("aoLast", 0);
    prefs.end();
    return v;
}

void Config::saveLastAutoOpenDate(uint32_t yyyymmdd) {
    Preferences prefs;
    if (prefs.begin(NS, false)) {
        prefs.putUInt("aoLast", yyyymmdd);
        prefs.end();
    }
}
