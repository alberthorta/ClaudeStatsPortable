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
