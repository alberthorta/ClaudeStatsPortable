#include "api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>

static String genUuidV4() {
    uint8_t b[16];
    for (int i = 0; i < 16; i++) b[i] = (uint8_t)(esp_random() & 0xFF);
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return String(buf);
}

static void addBrowserHeaders(HTTPClient& http, const String& sessionKey) {
    http.addHeader("Cookie",     "sessionKey=" + sessionKey);
    http.addHeader("Accept",     "application/json");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Origin",     "https://claude.ai");
    http.addHeader("Referer",    "https://claude.ai/chats");
    http.addHeader("User-Agent",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
}

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

// ─── openWindow: daily 5h-window trigger (with verbose logging) ─────────────
//
// First pass is intentionally verbose — we don't know exactly which endpoint
// claude.ai expects right now. The Serial log lets us iterate: flash, trigger,
// read HTTP status + response body, adjust payload.
//
// Planned flow, once we know the right shape:
//   1) POST /api/organizations/{org}/chat_conversations      (create)
//   2) POST /api/organizations/{org}/chat_conversations/{c}/completion
//      -> this is what actually consumes the window; body includes prompt etc.
//   3) DELETE /api/organizations/{org}/chat_conversations/{c}
//
// For now, step 1 is real, step 2/3 are attempted and whatever comes back is
// logged so we can debug.
Api::Result Api::openWindow(const AppConfig& cfg) {
    if (cfg.orgId.isEmpty() || cfg.sessionKey.isEmpty()) {
        Serial.println("[openWindow] missing orgId or sessionKey");
        return Result::Unauthorized;
    }

    String convUuid = genUuidV4();
    String base     = "https://claude.ai/api/organizations/" + cfg.orgId + "/chat_conversations";
    Serial.println("=== openWindow begin ===");
    Serial.print  ("[openWindow] orgId="); Serial.println(cfg.orgId);
    Serial.print  ("[openWindow] convUuid="); Serial.println(convUuid);

    WiFiClientSecure client;
    client.setInsecure();

    // ── 1) Create conversation ─────────────────────────────────────────────
    {
        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(10000);
        if (!http.begin(client, base)) {
            Serial.println("[openWindow] create: http.begin failed");
            return Result::Network;
        }
        addBrowserHeaders(http, cfg.sessionKey);

        String body = "{\"uuid\":\"" + convUuid + "\",\"name\":\"\"}";
        Serial.print("[openWindow] POST "); Serial.println(base);
        Serial.print("[openWindow] body: ");  Serial.println(body);

        int code = http.POST(body);
        String resp = http.getString();
        Serial.print("[openWindow] create status="); Serial.println(code);
        Serial.print("[openWindow] create resp: ");  Serial.println(resp);
        http.end();

        if (code == 401 || code == 403) return Result::Unauthorized;
        if (code < 200 || code >= 300)  return Result::Network;
    }

    // ── 2) Send minimal completion (this is what opens the 5h window) ──────
    {
        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(15000);
        String url = base + "/" + convUuid + "/completion";
        if (!http.begin(client, url)) {
            Serial.println("[openWindow] completion: http.begin failed");
            return Result::Network;
        }
        addBrowserHeaders(http, cfg.sessionKey);
        http.addHeader("Accept", "text/event-stream");

        // First guess at body shape. We'll iterate based on the Serial output.
        String body = "{"
            "\"prompt\":\"hi\","
            "\"parent_message_uuid\":\"00000000-0000-4000-8000-000000000000\","
            "\"timezone\":\"UTC\","
            "\"attachments\":[],"
            "\"files\":[],"
            "\"sync_sources\":[],"
            "\"rendering_mode\":\"messages\""
        "}";
        Serial.print("[openWindow] POST "); Serial.println(url);
        Serial.print("[openWindow] body: ");  Serial.println(body);

        int code = http.POST(body);
        String resp = http.getString();
        Serial.print("[openWindow] completion status="); Serial.println(code);
        Serial.print("[openWindow] completion resp (first 512): ");
        Serial.println(resp.substring(0, 512));
        http.end();
    }

    // ── 3) Delete the conversation so it doesn't clutter the sidebar ──────
    {
        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(10000);
        String url = base + "/" + convUuid;
        if (http.begin(client, url)) {
            addBrowserHeaders(http, cfg.sessionKey);
            int code = http.sendRequest("DELETE");
            Serial.print("[openWindow] delete status="); Serial.println(code);
            http.end();
        }
    }

    Serial.println("=== openWindow end ===");
    return Result::Ok;
}
