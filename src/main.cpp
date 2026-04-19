#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "provisioning.h"
#include "api.h"
#include "stats.h"
#include "webconfig.h"

static const int           KEY_BTN          = 14;
static const unsigned long RESET_HOLD_MS    = 10000;
static const unsigned long SHORT_MIN_MS     = 50;
static const unsigned long WIFI_TIMEOUT_MS  = 20000;
static const unsigned long REFRESH_MS       = 30000;

enum class View { Stats, Info };

static AppConfig      cfg;
static Usage          lastUsage;
static bool           lastOk           = false;
static bool           haveGoodData     = false;
static String         lastError;
static unsigned long  lastFetchMs      = 0;
static unsigned long  lastUiTickMs     = 0;
static unsigned long  keyPressStart    = 0;
static bool           keyWasDown       = false;
static bool           resetFired       = false;
static View           view             = View::Stats;

static bool connectToStoredWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
    Display::showConnecting(cfg.ssid);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

static void syncNtp() {
    setenv("TZ", "UTC0", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    Display::showLoading("Syncing time...");
    unsigned long start = millis();
    while (time(nullptr) < 1700000000 && millis() - start < 10000) {
        delay(200);
    }
}

static int secondsUntilNextFetch() {
    unsigned long elapsed = millis() - lastFetchMs;
    if (elapsed >= REFRESH_MS) return 0;
    return (int)((REFRESH_MS - elapsed) / 1000);
}

static void doFetch() {
    Usage fresh;
    Api::Result r = Api::fetchUsage(cfg, fresh);
    lastFetchMs = millis();
    switch (r) {
        case Api::Result::Ok:
            lastUsage    = fresh;
            lastOk       = true;
            haveGoodData = true;
            lastError    = "";
            break;
        case Api::Result::Unauthorized:
            lastOk    = false;
            lastError = "Session expired. Edit at device IP or reset.";
            break;
        case Api::Result::Network:
            lastOk    = false;
            lastError = "Network error.";
            break;
        case Api::Result::Parse:
            lastOk    = false;
            lastError = "Bad response from server.";
            break;
    }
}

static void redraw() {
    if (view == View::Info) {
        Display::showInfo(cfg, WiFi.localIP().toString());
        return;
    }
    time_t now = time(nullptr);
    int secs = secondsUntilNextFetch();
    if (haveGoodData) {
        Display::showStats(lastUsage, now, secs, /*stale=*/!lastOk);
    } else {
        Display::showApiError(lastError, secs);
    }
}

static void toggleView() {
    view = (view == View::Stats) ? View::Info : View::Stats;
    redraw();
}

static void handleKeyButton() {
    bool pressed = digitalRead(KEY_BTN) == LOW;

    if (pressed && !keyWasDown) {
        keyPressStart = millis();
        keyWasDown    = true;
        resetFired    = false;
    } else if (pressed && keyWasDown && !resetFired &&
               millis() - keyPressStart >= RESET_HOLD_MS) {
        resetFired = true;
        Display::showResetting();
        delay(800);
        Config::clearWifi();
        ESP.restart();
    } else if (!pressed && keyWasDown) {
        unsigned long held = millis() - keyPressStart;
        keyWasDown = false;
        if (!resetFired && held >= SHORT_MIN_MS && held < RESET_HOLD_MS) {
            toggleView();
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(KEY_BTN, INPUT_PULLUP);
    Display::begin();

    if (!Config::load(cfg)) {
        cfg = Provisioning::run(cfg);
        delay(500);
        ESP.restart();
    }

    if (!connectToStoredWifi()) {
        Display::showApiError("WiFi connect failed.", 0);
        delay(3000);
        ESP.restart();
    }

    syncNtp();

    if (cfg.orgId.isEmpty()) {
        Display::showLoading("Discovering organization...");
        String orgId;
        Api::Result r = Api::fetchOrgId(cfg.sessionKey, orgId);
        if (r == Api::Result::Ok) {
            cfg.orgId = orgId;
            Config::save(cfg);
        } else {
            lastOk = false;
            lastError = (r == Api::Result::Unauthorized)
                ? "Invalid sessionKey. Edit at device IP."
                : "Could not discover organization.";
        }
    }

    WebConfig::begin(cfg);

    Display::showLoading("Fetching usage...");
    if (!cfg.orgId.isEmpty()) doFetch();
    else lastFetchMs = millis();
    redraw();
    lastUiTickMs = millis();
}

void loop() {
    handleKeyButton();
    WebConfig::loop();

    unsigned long nowMs = millis();

    if (nowMs - lastFetchMs >= REFRESH_MS) {
        doFetch();
        redraw();
        lastUiTickMs = nowMs;
    } else if (nowMs - lastUiTickMs >= 1000) {
        redraw();
        lastUiTickMs = nowMs;
    }

    delay(10);
}
