#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "provisioning.h"
#include "api.h"
#include "stats.h"
#include "webconfig.h"
#include "battery.h"

static const int           KEY_BTN              = 14;
static const int           BOOT_BTN             = 0;
static const unsigned long RESET_HOLD_MS        = 10000;
static const unsigned long SCREEN_SLEEP_HOLD_MS = 2000;
static const unsigned long SHORT_MIN_MS         = 50;
static const unsigned long SHORT_MAX_MS         = 2000;
static const unsigned long WIFI_TIMEOUT_MS      = 20000;
static const unsigned long REFRESH_MS           = 30000;

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
static unsigned long  bootPressStart   = 0;
static bool           bootWasDown      = false;
static bool           bootSleepFired   = false;
static bool           bootWakeOnly     = false;
static bool           keyWakeOnly      = false;
static bool           screenOn         = true;
static View           view             = View::Stats;
static uint32_t       lastAutoOpenDate = 0;  // YYYYMMDD UTC

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

static void checkAutoOpen() {
    if (!cfg.autoOpenEnabled) return;
    time_t now = time(nullptr);
    if (now < 1700000000) return;  // NTP not synced yet
    struct tm g;
    if (!gmtime_r(&now, &g)) return;
    uint32_t today = (uint32_t)(g.tm_year + 1900) * 10000u
                   + (uint32_t)(g.tm_mon + 1) * 100u
                   + (uint32_t)g.tm_mday;
    if (today == lastAutoOpenDate) return;

    bool timeReached = g.tm_hour > cfg.autoOpenHourUtc
                    || (g.tm_hour == cfg.autoOpenHourUtc && g.tm_min >= cfg.autoOpenMinuteUtc);
    if (!timeReached) return;

    Serial.printf("[auto-open] firing for %04d-%02d-%02d (cfg %02d:%02d UTC)\n",
                  g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  cfg.autoOpenHourUtc, cfg.autoOpenMinuteUtc);
    Api::Result r = Api::openWindow(cfg);
    Serial.printf("[auto-open] result=%d\n", (int)r);

    // Mark the day as attempted either way so a transient failure doesn't
    // spam claude.ai. One shot per day.
    lastAutoOpenDate = today;
    Config::saveLastAutoOpenDate(today);
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
    int batMv = Battery::millivolts();
    if (view == View::Info) {
        Display::showInfo(cfg, WiFi.localIP().toString(), batMv);
        return;
    }
    time_t now = time(nullptr);
    int secs = secondsUntilNextFetch();
    if (haveGoodData) {
        Display::showStats(lastUsage, now, batMv, secs, /*stale=*/!lastOk);
    } else {
        Display::showApiError(lastError, secs);
    }
}

static void toggleView() {
    view = (view == View::Stats) ? View::Info : View::Stats;
    redraw();
}

static void wakeScreen() {
    if (screenOn) return;
    screenOn = true;
    Display::setBacklight(true);
    redraw();
}

static void sleepScreen() {
    if (!screenOn) return;
    screenOn = false;
    Display::setBacklight(false);
}

static void handleBootButton() {
    bool pressed = digitalRead(BOOT_BTN) == LOW;
    if (pressed && !bootWasDown) {
        bootPressStart = millis();
        bootWasDown    = true;
        bootSleepFired = false;
        // If the screen was off, this press is a wake-up only: consume it
        // so the release doesn't also rotate.
        bootWakeOnly   = !screenOn;
        wakeScreen();
    } else if (pressed && bootWasDown && !bootSleepFired && !bootWakeOnly &&
               millis() - bootPressStart >= SCREEN_SLEEP_HOLD_MS) {
        bootSleepFired = true;
        sleepScreen();
    } else if (!pressed && bootWasDown) {
        unsigned long held = millis() - bootPressStart;
        bootWasDown = false;
        if (!bootSleepFired && !bootWakeOnly &&
            held >= SHORT_MIN_MS && held < SHORT_MAX_MS) {
            int r = (Display::rotation() + 1) & 3;
            Display::setRotation(r);
            Config::saveRotation(r);
            redraw();
        }
    }
}

static void handleKeyButton() {
    bool pressed = digitalRead(KEY_BTN) == LOW;

    if (pressed && !keyWasDown) {
        keyPressStart = millis();
        keyWasDown    = true;
        resetFired    = false;
        keyWakeOnly   = !screenOn;
        wakeScreen();
    } else if (pressed && keyWasDown && !resetFired && !keyWakeOnly &&
               millis() - keyPressStart >= RESET_HOLD_MS) {
        resetFired = true;
        Display::showResetting();
        delay(800);
        Config::clearWifi();
        ESP.restart();
    } else if (!pressed && keyWasDown) {
        unsigned long held = millis() - keyPressStart;
        keyWasDown = false;
        if (!resetFired && !keyWakeOnly &&
            held >= SHORT_MIN_MS && held < RESET_HOLD_MS) {
            toggleView();
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(KEY_BTN,  INPUT_PULLUP);
    pinMode(BOOT_BTN, INPUT_PULLUP);
    Display::begin();
    Display::setRotation(Config::loadRotation(1));

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

    lastAutoOpenDate = Config::loadLastAutoOpenDate();

    Display::showLoading("Fetching usage...");
    if (!cfg.orgId.isEmpty()) doFetch();
    else lastFetchMs = millis();
    redraw();
    lastUiTickMs = millis();
}

void loop() {
    handleKeyButton();
    handleBootButton();
    WebConfig::loop();

    unsigned long nowMs = millis();

    if (nowMs - lastFetchMs >= REFRESH_MS) {
        doFetch();
        if (screenOn) redraw();
        lastUiTickMs = nowMs;
    } else if (nowMs - lastUiTickMs >= 1000) {
        checkAutoOpen();
        if (screenOn) redraw();
        lastUiTickMs = nowMs;
    }

    delay(10);
}
