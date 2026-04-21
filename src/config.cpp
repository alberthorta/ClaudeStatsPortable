#include "config.h"
#include <Preferences.h>

static const char* NS = "claudestats";

static void wifiKeys(int i, char* ssidKey, char* passKey) {
    snprintf(ssidKey, 8, "w%ds", i);
    snprintf(passKey, 8, "w%dp", i);
}

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
    out.idleSleepMin      = prefs.getInt  ("idleMin", 5);
    out.adminUser         = prefs.getString("adminU", "admin");
    out.adminPassword     = prefs.getString("adminP", "admin");

    int count = prefs.getInt("wN", 0);
    if (count < 0) count = 0;
    if (count > MAX_WIFIS) count = MAX_WIFIS;
    out.wifiCount = count;
    for (int i = 0; i < count; i++) {
        char sk[8], pk[8]; wifiKeys(i, sk, pk);
        out.wifis[i].ssid     = prefs.getString(sk, "");
        out.wifis[i].password = prefs.getString(pk, "");
    }
    prefs.end();

    // Migration: if legacy primary ssid is set but the list is empty, seed
    // the list from it so we don't lose the previous credential on upgrade.
    if (out.wifiCount == 0 && out.ssid.length() > 0) {
        out.wifis[0].ssid     = out.ssid;
        out.wifis[0].password = out.password;
        out.wifiCount = 1;
    }
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
    prefs.putInt  ("idleMin", cfg.idleSleepMin);
    prefs.putString("adminU", cfg.adminUser);
    prefs.putString("adminP", cfg.adminPassword);

    // Rewrite the list: write the current count + entries, and clear any
    // stale trailing ones from a previously-larger list.
    prefs.putInt("wN", cfg.wifiCount);
    for (int i = 0; i < cfg.wifiCount; i++) {
        char sk[8], pk[8]; wifiKeys(i, sk, pk);
        prefs.putString(sk, cfg.wifis[i].ssid);
        prefs.putString(pk, cfg.wifis[i].password);
    }
    for (int i = cfg.wifiCount; i < MAX_WIFIS; i++) {
        char sk[8], pk[8]; wifiKeys(i, sk, pk);
        prefs.remove(sk);
        prefs.remove(pk);
    }

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

int Config::findWifi(const AppConfig& cfg, const String& ssid) {
    for (int i = 0; i < cfg.wifiCount; i++) {
        if (cfg.wifis[i].ssid == ssid) return i;
    }
    return -1;
}

bool Config::addOrUpdateWifi(AppConfig& cfg, const String& ssid, const String& password) {
    int idx = findWifi(cfg, ssid);
    if (idx >= 0) {
        cfg.wifis[idx].password = password;
        return true;
    }
    if (cfg.wifiCount >= MAX_WIFIS) return false;
    cfg.wifis[cfg.wifiCount].ssid     = ssid;
    cfg.wifis[cfg.wifiCount].password = password;
    cfg.wifiCount++;
    return true;
}

bool Config::removeWifiAt(AppConfig& cfg, int index) {
    if (index < 0 || index >= cfg.wifiCount) return false;
    for (int i = index; i < cfg.wifiCount - 1; i++) {
        cfg.wifis[i] = cfg.wifis[i + 1];
    }
    cfg.wifis[cfg.wifiCount - 1] = WifiCred{};
    cfg.wifiCount--;
    return true;
}

bool Config::moveWifi(AppConfig& cfg, int index, int delta) {
    int dst = index + delta;
    if (index < 0 || index >= cfg.wifiCount) return false;
    if (dst < 0 || dst >= cfg.wifiCount) return false;
    WifiCred tmp = cfg.wifis[index];
    cfg.wifis[index] = cfg.wifis[dst];
    cfg.wifis[dst]   = tmp;
    return true;
}
