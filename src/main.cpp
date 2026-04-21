#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>
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
static const unsigned long COMBO_HOLD_MS           = 5000;
static const unsigned long ARM_COUNTDOWN_DEEP_MS   = 5000;
static const unsigned long ARM_COUNTDOWN_REBOOT_MS = 5000;
static const unsigned long ARM_COUNTDOWN_SHORT_MS  = 3000;
static const unsigned long BOOT_FACTORY_HOLD_MS    = 2000;
static const unsigned long LIGHT_SLEEP_TIMER_US    = 30ULL * 1000000ULL;

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

enum class SleepMode { Normal, Combo, Armed };
enum class PendingAction { None, BacklightOff, LightSleep, DeepSleep, Reboot };
static SleepMode      sleepMode         = SleepMode::Normal;
static PendingAction  pendingAction     = PendingAction::None;
static unsigned long  modeStartMs       = 0;
static int            lastCountdownSec  = -1;
static bool           comboReleasedOnce = false;
static unsigned long  lastInteractionMs = 0;
static unsigned long  bootHoldStartMs      = 0;  // for Reboot-armed factory-reset escalation
static int            lastBootSecsShown    = -1;
static bool           forceFetchOnNextTick = false;  // set on wake from any sleep
static bool           inLightSleepCycle    = false;  // true while doing tier 2 timer-refresh loops

static bool connectAnyKnown() {
    WiFi.mode(WIFI_STA);
    Display::showLoading("Scanning WiFi…");
    int n = WiFi.scanNetworks();

    // Build a bitmap of which saved entries are currently visible in the air.
    // List order IS the priority; we try visible-known entries top-down.
    bool visible[MAX_WIFIS] = { false };
    int  rssi[MAX_WIFIS];
    for (int i = 0; i < cfg.wifiCount; i++) rssi[i] = 0;
    for (int i = 0; i < n; i++) {
        int j = Config::findWifi(cfg, WiFi.SSID(i));
        if (j >= 0 && !visible[j]) {
            visible[j] = true;
            rssi[j]    = WiFi.RSSI(i);
        }
    }
    WiFi.scanDelete();

    int visibleCount = 0;
    for (int i = 0; i < cfg.wifiCount; i++) if (visible[i]) visibleCount++;

    if (visibleCount == 0) {
        Serial.println("[wifi] no saved network is currently visible");
        // Fallback: try the legacy primary SSID in case the scan missed it.
        if (cfg.ssid.length() > 0) {
            Display::showConnecting(cfg.ssid);
            WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
                delay(250);
            }
            return WiFi.status() == WL_CONNECTED;
        }
        return false;
    }

    for (int i = 0; i < cfg.wifiCount; i++) {
        if (!visible[i]) continue;
        const WifiCred& c = cfg.wifis[i];
        Serial.printf("[wifi] trying %s (priority %d, %d dBm)\n",
                      c.ssid.c_str(), i, rssi[i]);
        Display::showConnecting(c.ssid);
        WiFi.begin(c.ssid.c_str(), c.password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
            delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
            if (cfg.ssid != c.ssid || cfg.password != c.password) {
                cfg.ssid     = c.ssid;
                cfg.password = c.password;
                Config::save(cfg);
            }
            Serial.printf("[wifi] connected to %s\n", c.ssid.c_str());
            return true;
        }
        WiFi.disconnect(false, true);
    }
    return false;
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
    // Coming back from tier 1 (backlight off) or tier 2 (light sleep) — ask
    // the main loop to fetch fresh data on the next iteration so the first
    // frame the user looks at isn't based on cached values.
    forceFetchOnNextTick = true;
    redraw();
}

static void sleepScreen() {
    if (!screenOn) return;
    screenOn = false;
    Display::setBacklight(false);
}

static void markInteraction() {
    lastInteractionMs = millis();
}

static void enterDeepSleep();    // fwd decl
static void enterLightSleep();   // fwd decl

// Transitions the state machine into Armed with the given pending action.
// waitForRelease: true when a button was already held at the moment of arming
// (tier 1 from BOOT-hold, tier 3 from BOOT+KEY combo); the countdown doesn't
// accept cancel presses until every button has been released at least once.
// For tier 2 (automatic from idle), no button is held, so waitForRelease
// should be false.
static void armSleep(PendingAction action, bool waitForRelease) {
    sleepMode         = SleepMode::Armed;
    pendingAction     = action;
    modeStartMs       = millis();
    lastCountdownSec  = -1;
    lastBootSecsShown = -1;
    bootHoldStartMs   = 0;
    comboReleasedOnce = !waitForRelease;
    // While the countdown is visible, backlight must be on.
    Display::setBacklight(true);
    screenOn = true;
}

// Returns true if combo/armed modes are active and consumed input this tick,
// meaning the individual button handlers should be skipped.
static bool handleSleepModes() {
    bool bp = digitalRead(BOOT_BTN) == LOW;
    bool kp = digitalRead(KEY_BTN)  == LOW;
    bool any  = bp || kp;
    bool both = bp && kp;
    unsigned long now = millis();

    switch (sleepMode) {
        case SleepMode::Normal:
            if (both) {
                sleepMode    = SleepMode::Combo;
                modeStartMs  = now;
                // suppress any in-flight individual press state so the
                // release edges don't fire rotate / toggle-view later.
                keyWasDown   = false;
                bootWasDown  = false;
                return true;
            }
            return false;

        case SleepMode::Combo:
            if (!both) {
                // Combo released before the 5 s threshold: silently cancel.
                sleepMode   = SleepMode::Normal;
                keyWasDown  = false;
                bootWasDown = false;
            } else if (now - modeStartMs >= COMBO_HOLD_MS) {
                armSleep(PendingAction::DeepSleep, /*waitForRelease=*/true);
            }
            return true;

        case SleepMode::Armed: {
            if (!any) comboReleasedOnce = true;

            const bool isReboot = (pendingAction == PendingAction::Reboot);

            // Reboot-armed: BOOT is the escalator (hold 2s → factory reset).
            // Track the hold here so it works before the main countdown
            // expires and regardless of cancel semantics.
            if (isReboot) {
                if (bp) {
                    if (bootHoldStartMs == 0) bootHoldStartMs = now;
                } else {
                    bootHoldStartMs = 0;
                }
                if (bootHoldStartMs && now - bootHoldStartMs >= BOOT_FACTORY_HOLD_MS) {
                    Serial.println("[reset] factory reset — clearing all NVS");
                    Display::showResetting();
                    delay(400);
                    Config::clear();
                    ESP.restart();  // no return
                    return true;
                }
            }

            // Cancel rules:
            //  - Reboot-armed: BOOT is the escalator, so only KEY cancels.
            //  - Every other pending action: any button cancels.
            bool cancelNow = isReboot
                ? (kp && comboReleasedOnce)
                : (any && comboReleasedOnce);

            if (cancelNow) {
                // Stay in normal operation. Consume the cancel press so its
                // release doesn't also fire rotate / toggle view.
                sleepMode       = SleepMode::Normal;
                pendingAction   = PendingAction::None;
                keyWasDown      = kp;
                bootWasDown     = bp;
                keyWakeOnly     = kp;
                bootWakeOnly    = bp;
                keyPressStart   = now;
                bootPressStart  = now;
                bootSleepFired  = true;  // suppress another BOOT-2s arm
                bootHoldStartMs = 0;
                markInteraction();
                redraw();
                return true;
            }

            unsigned long duration;
            switch (pendingAction) {
                case PendingAction::DeepSleep: duration = ARM_COUNTDOWN_DEEP_MS;   break;
                case PendingAction::Reboot:    duration = ARM_COUNTDOWN_REBOOT_MS; break;
                default:                       duration = ARM_COUNTDOWN_SHORT_MS;  break;
            }

            unsigned long elapsed = now - modeStartMs;
            if (elapsed >= duration) {
                PendingAction a = pendingAction;
                sleepMode     = SleepMode::Normal;
                pendingAction = PendingAction::None;
                switch (a) {
                    case PendingAction::BacklightOff:
                        sleepScreen();
                        break;
                    case PendingAction::LightSleep:
                        sleepScreen();
                        inLightSleepCycle = true;  // subsequent timer wakes are silent
                        enterLightSleep();
                        break;
                    case PendingAction::DeepSleep:
                        enterDeepSleep();  // no return
                        break;
                    case PendingAction::Reboot:
                        ESP.restart();  // no return
                        break;
                    default: break;
                }
                return true;
            }

            int secsLeft = (int)((duration - elapsed + 999) / 1000);

            // When BOOT is actively held during Reboot-armed, swap the
            // display to a factory-reset sub-countdown so the user sees
            // what's happening.
            if (isReboot && bootHoldStartMs) {
                unsigned long held = now - bootHoldStartMs;
                int bootSecs = (int)((BOOT_FACTORY_HOLD_MS - held + 999) / 1000);
                if (bootSecs < 0) bootSecs = 0;
                if (bootSecs != lastBootSecsShown) {
                    Display::showSleepArmed("Factory reset in", bootSecs,
                                             "keep holding BOOT…");
                    lastBootSecsShown = bootSecs;
                    lastCountdownSec  = -1;  // force reboot-display redraw on release
                }
            } else if (secsLeft != lastCountdownSec) {
                const char* title    = "Sleeping in";
                const char* subtitle = nullptr;
                switch (pendingAction) {
                    case PendingAction::BacklightOff: title = "Screen off in";  break;
                    case PendingAction::LightSleep:   title = "Light sleep in"; break;
                    case PendingAction::DeepSleep:    title = "Deep sleep in";  break;
                    case PendingAction::Reboot:
                        title    = "Reboot in";
                        subtitle = "KEY cancel · BOOT reset";
                        break;
                    default: break;
                }
                Display::showSleepArmed(title, secsLeft, subtitle);
                lastCountdownSec  = secsLeft;
                lastBootSecsShown = -1;
            }
            return true;
        }
    }
    return false;
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
        markInteraction();
    } else if (pressed && bootWasDown && !bootSleepFired && !bootWakeOnly &&
               millis() - bootPressStart >= SCREEN_SLEEP_HOLD_MS) {
        bootSleepFired = true;
        // Arm tier 1 with the same cancellable countdown as tier 2/3. BOOT
        // is still held, so waitForRelease=true to avoid auto-cancel.
        armSleep(PendingAction::BacklightOff, /*waitForRelease=*/true);
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
        markInteraction();
    } else if (pressed && keyWasDown && !resetFired && !keyWakeOnly &&
               millis() - keyPressStart >= RESET_HOLD_MS) {
        resetFired = true;
        // Arm a cancellable 5 s reboot countdown. While it's visible, the
        // user can escalate to a full factory reset by holding BOOT for 2 s.
        armSleep(PendingAction::Reboot, /*waitForRelease=*/true);
    } else if (!pressed && keyWasDown) {
        unsigned long held = millis() - keyPressStart;
        keyWasDown = false;
        if (!resetFired && !keyWakeOnly &&
            held >= SHORT_MIN_MS && held < RESET_HOLD_MS) {
            toggleView();
        }
    }
}

static void configureButtonWakeup() {
    esp_sleep_enable_ext1_wakeup(
        (1ULL << BOOT_BTN) | (1ULL << KEY_BTN),
        ESP_EXT1_WAKEUP_ANY_LOW);
}

static void enterLightSleep() {
    Serial.println("[sleep] light sleep");
    Display::panelSleep(true);

    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_TIMER_US);
    configureButtonWakeup();

    esp_light_sleep_start();  // blocks until wake

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Display::panelSleep(false);
    Serial.printf("[sleep] light wake cause=%d\n", (int)cause);

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        // Silent background refresh — keep screen off so it stays low-power.
        checkAutoOpen();
        doFetch();
        // Don't mark interaction: we want to fall back into light sleep.
    } else {
        // Button wake: peripherals (GPIO pull-ups, TFT parallel bus, WiFi
        // state) can come back in a half-initialised state after light sleep,
        // and users notice as unresponsive buttons or a frozen UI. A clean
        // reboot is fast enough (~5 s with WiFi reconnect) and guarantees
        // a known-good state plus a fresh fetch via setup().
        Serial.println("[sleep] button wake from light sleep — restarting");
        ESP.restart();  // no return
    }
}

static void enterDeepSleep() {
    Serial.println("[sleep] DEEP sleep");
    Display::showDeepSleep();
    delay(400);
    Display::panelSleep(true);
    digitalWrite(TFT_BL, LOW);
    digitalWrite(15, LOW);  // cut the LCD power rail

    configureButtonWakeup();

    // RTC timer to wake for the daily auto-open, if enabled and NTP is synced.
    if (cfg.autoOpenEnabled) {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            struct tm g;
            gmtime_r(&now, &g);
            struct tm next = g;
            next.tm_hour = cfg.autoOpenHourUtc;
            next.tm_min  = cfg.autoOpenMinuteUtc;
            next.tm_sec  = 0;
            time_t tt = mktime(&next);
            if (tt <= now) tt += 86400;
            uint64_t secs_until = (uint64_t)(tt - now);
            Serial.printf("[sleep] deep timer: %llu s until next auto-open\n",
                          (unsigned long long)secs_until);
            esp_sleep_enable_timer_wakeup(secs_until * 1000000ULL);
        }
    }

    esp_deep_sleep_start();  // no return
}

void setup() {
    Serial.begin(115200);
    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    Serial.printf("[boot] wake cause=%d\n", (int)wakeCause);

    pinMode(KEY_BTN,  INPUT_PULLUP);
    pinMode(BOOT_BTN, INPUT_PULLUP);
    Display::begin();
    Display::setRotation(Config::loadRotation(1));

    if (!Config::load(cfg)) {
        cfg = Provisioning::run(cfg);
        delay(500);
        ESP.restart();
    }

    if (!connectAnyKnown()) {
        Display::showApiError("No known WiFi in range.", 0);
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

    // Deep-sleep timer wake: we're here only to fire the daily auto-open,
    // then go straight back to deep sleep. Don't light up the screen.
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("[boot] timer wake — firing auto-open then re-sleeping");
        if (cfg.autoOpenEnabled) {
            Api::openWindow(cfg);
            time_t now = time(nullptr);
            if (now > 1700000000) {
                struct tm g;
                if (gmtime_r(&now, &g)) {
                    uint32_t today = (uint32_t)(g.tm_year + 1900) * 10000u
                                   + (uint32_t)(g.tm_mon + 1) * 100u
                                   + (uint32_t)g.tm_mday;
                    lastAutoOpenDate = today;
                    Config::saveLastAutoOpenDate(today);
                }
            }
        }
        enterDeepSleep();  // no return
    }

    Display::showLoading("Fetching usage...");
    if (!cfg.orgId.isEmpty()) doFetch();
    else lastFetchMs = millis();
    redraw();
    lastUiTickMs = millis();
    markInteraction();
}

void loop() {
    // Combo / armed-countdown state machine. When active it suppresses the
    // normal button handlers and most of the UI tick so the countdown frame
    // stays stable.
    bool modeActive = handleSleepModes();

    if (!modeActive) {
        handleKeyButton();
        handleBootButton();
    }
    WebConfig::loop();

    unsigned long nowMs = millis();

    if (forceFetchOnNextTick || nowMs - lastFetchMs >= REFRESH_MS) {
        forceFetchOnNextTick = false;
        doFetch();
        if (screenOn && !modeActive) redraw();
        lastUiTickMs = nowMs;
    } else if (nowMs - lastUiTickMs >= 1000) {
        checkAutoOpen();
        if (screenOn && !modeActive) redraw();
        lastUiTickMs = nowMs;
    }

    // Tier 2: auto light sleep after idleSleepMin of backlight-off idle.
    // The arming step lights the screen back up to show a cancellable
    // countdown; if the user presses anything during the countdown, we drop
    // back to full normal operation (screen stays on) instead of sliding
    // back to tier 1. Once we've entered the cycle, subsequent timer wakes
    // re-enter silently — the user only sees the countdown on the very first
    // transition from tier 1.
    if (!modeActive && !screenOn && cfg.idleSleepMin > 0) {
        if (inLightSleepCycle) {
            enterLightSleep();  // silent re-entry after the 30 s timer tick
        } else {
            unsigned long idleMs = nowMs - lastInteractionMs;
            unsigned long threshold = (unsigned long)cfg.idleSleepMin * 60UL * 1000UL;
            if (idleMs >= threshold) {
                armSleep(PendingAction::LightSleep, /*waitForRelease=*/false);
            }
        }
    }

    delay(10);
}
