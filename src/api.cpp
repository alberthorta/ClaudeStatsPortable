#include "api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static time_t parseIso8601Utc(const char* s) {
    if (!s || !*s) return 0;
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
    // TZ is set to UTC0 in main.cpp, so mktime returns UTC epoch.
    return mktime(&tm);
}

Api::Result Api::fetchOrgId(const String& sessionKey, String& out) {
    out = "";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);

    if (!http.begin(client, "https://claude.ai/api/organizations")) return Result::Network;

    http.addHeader("Cookie",     "sessionKey=" + sessionKey);
    http.addHeader("Accept",     "application/json");
    http.addHeader("User-Agent", "ClaudeStatsPortable/0.1 (ESP32)");

    int code = http.GET();
    if (code == 401 || code == 403) {
        http.end();
        return Result::Unauthorized;
    }
    if (code != 200) {
        http.end();
        return Result::Network;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return Result::Parse;

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) return Result::NoOrg;

    const char* uuid = arr[0]["uuid"] | (const char*)nullptr;
    if (!uuid || !*uuid) return Result::NoOrg;

    out = String(uuid);
    return Result::Ok;
}

Api::Result Api::fetchUsage(const AppConfig& cfg, Usage& out) {
    out = Usage{};

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);

    String url = "https://claude.ai/api/organizations/" + cfg.orgId + "/usage";
    if (!http.begin(client, url)) return Result::Network;

    http.addHeader("Cookie",     "sessionKey=" + cfg.sessionKey);
    http.addHeader("Accept",     "application/json");
    http.addHeader("User-Agent", "ClaudeStatsPortable/0.1 (ESP32)");

    int code = http.GET();
    if (code == 401 || code == 403) {
        http.end();
        return Result::Unauthorized;
    }
    if (code != 200) {
        http.end();
        return Result::Network;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return Result::Parse;

    auto parseWindow = [](JsonVariantConst v, UsageWindow& w) {
        if (v.isNull()) return;
        w.valid = true;
        if (!v["utilization"].isNull()) {
            w.utilization = v["utilization"].as<double>();
        }
        const char* iso = v["resets_at"] | (const char*)nullptr;
        w.resetsAt = parseIso8601Utc(iso);
    };

    parseWindow(doc["five_hour"], out.fiveHour);
    parseWindow(doc["seven_day"], out.sevenDay);
    out.valid = out.fiveHour.valid || out.sevenDay.valid;

    return Result::Ok;
}
