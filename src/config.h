#pragma once
#include <Arduino.h>

struct AppConfig {
    String ssid;
    String password;
    String sessionKey;
    String orgId;

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
}
