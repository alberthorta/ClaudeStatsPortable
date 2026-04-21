#include "display.h"
#include "battery.h"
#include <TFT_eSPI.h>

static TFT_eSPI      tft;
static TFT_eSprite   sprite = TFT_eSprite(&tft);
static bool          spriteReady   = false;
static int           currentRot    = 1;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const uint16_t COLOR_BG        = rgb565(  8,  10,  16);
static const uint16_t COLOR_DIVIDER   = rgb565( 40,  44,  56);
static const uint16_t COLOR_TEXT      = TFT_WHITE;
static const uint16_t COLOR_MUTED     = rgb565(140, 146, 160);
static const uint16_t COLOR_BAR_BG    = rgb565( 32,  36,  46);
static const uint16_t COLOR_MARKER    = TFT_WHITE;
static const uint16_t COLOR_ACCENT    = rgb565( 90, 170, 255);

static int16_t screenW() { return tft.width();  }
static int16_t screenH() { return tft.height(); }
static bool    isPortrait() { return screenW() < screenH(); }

// ─── Sprite lifecycle ──────────────────────────────────────────────────────

static void destroySprite() {
    if (spriteReady) { sprite.deleteSprite(); }
    spriteReady = false;
}

static void createSprite() {
    destroySprite();
    sprite.setColorDepth(16);
    sprite.createSprite(tft.width(), tft.height());
    spriteReady = sprite.created();
}

static void flush() {
    if (spriteReady) sprite.pushSprite(0, 0);
}

static TFT_eSPI& gfx() {
    return spriteReady ? (TFT_eSPI&)sprite : tft;
}

static void clear(uint16_t color = COLOR_BG) {
    if (spriteReady) sprite.fillSprite(color);
    else             tft.fillScreen(color);
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void Display::begin() {
    // GPIO15 = LCD_POWER_ON on the T-Display S3. When running on USB the
    // display rail is fed directly, but on battery the MCU has to drive this
    // pin HIGH or the panel goes dark (buttons keep working).
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    tft.init();
    currentRot = 1;
    tft.setRotation(currentRot);
    tft.fillScreen(COLOR_BG);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    createSprite();
}

void Display::setRotation(int r) {
    currentRot = r & 3;
    tft.setRotation(currentRot);
    tft.fillScreen(COLOR_BG);
    createSprite();
}

int Display::rotation() { return currentRot; }

void Display::setBacklight(bool on) {
    digitalWrite(TFT_BL, on ? HIGH : LOW);
}

void Display::panelSleep(bool in) {
    // ST7789 command set: 0x10 = SLPIN, 0x11 = SLPOUT.
    tft.writecommand(in ? 0x10 : 0x11);
    // Datasheet requires 5 ms after either command before the next one.
    delay(5);
}

void Display::showSleepArmed(const char* title, int secondsLeft,
                              const char* subtitle) {
    destroySprite();
    digitalWrite(TFT_BL, HIGH);  // force visible, ignoring tier-1 state
    tft.fillScreen(COLOR_BG);
    int W = tft.width(), H = tft.height();
    bool portrait = W < H;

    const char* cancel = subtitle ? subtitle
                        : (portrait ? "any button cancels"
                                    : "press any button to cancel");

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_MUTED, COLOR_BG);
    tft.drawString(title, W / 2, H / 2 - 54, 2);

    char buf[4]; snprintf(buf, sizeof(buf), "%d", secondsLeft);
    tft.setTextColor(TFT_ORANGE, COLOR_BG);
    tft.drawString(buf, W / 2, H / 2, 7);

    tft.setTextColor(COLOR_MUTED, COLOR_BG);
    tft.drawString(cancel, W / 2, H / 2 + 54, 2);

    createSprite();  // recreate so that if cancelled, normal redraws work
}

void Display::showDeepSleep() {
    destroySprite();
    digitalWrite(TFT_BL, HIGH);
    tft.fillScreen(COLOR_BG);
    int W = tft.width(), H = tft.height();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.drawString("Sleeping…", W / 2, H / 2, 4);
    // Sprite intentionally not recreated — the MCU is about to deep sleep
    // and the panel will be powered down shortly after.
}

// ─── Simple full-screen helpers (direct to tft) ─────────────────────────────

static void simpleHeader(const char* title, uint16_t color = TFT_CYAN) {
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, COLOR_BG);
    tft.drawString(title, 8, 8, 4);
    tft.drawFastHLine(0, 34, tft.width(), COLOR_DIVIDER);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
}

void Display::showProvisioning(const String& apSsid, const String& ip) {
    destroySprite();
    simpleHeader("Setup mode");
    tft.setCursor(8, 46, 2);
    tft.printf("Connect to WiFi:\n  %s\n\n", apSsid.c_str());
    tft.printf("Open browser at:\n  http://%s", ip.c_str());
    createSprite();
}

void Display::showConnecting(const String& ssid) {
    destroySprite();
    simpleHeader("Connecting");
    tft.setCursor(8, 56, 2);
    tft.printf("SSID: %s", ssid.c_str());
    createSprite();
}

void Display::showResetting() {
    destroySprite();
    simpleHeader("Reset", TFT_ORANGE);
    tft.setCursor(8, 56, 2);
    tft.print("Clearing config...");
    createSprite();
}

// ─── Sprite-backed screens ──────────────────────────────────────────────────

void Display::showLoading(const String& msg) {
    if (!spriteReady) createSprite();
    clear();
    auto& g = gfx();
    int W = screenW(), H = screenH();
    g.setTextDatum(MC_DATUM);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString("ClaudeStats", W/2, H/2 - 14, 4);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString(msg, W/2, H/2 + 14, 2);
    flush();
}

void Display::showApiError(const String& msg, int refreshInSec) {
    if (!spriteReady) createSprite();
    clear();
    auto& g = gfx();
    int W = screenW(), H = screenH();
    g.setTextDatum(TL_DATUM);
    g.setTextColor(TFT_RED, COLOR_BG);
    g.drawString("API error", 8, 8, 4);
    g.drawFastHLine(0, 36, W, COLOR_DIVIDER);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(msg, 8, 48, 2);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "Retry in %ds", refreshInSec);
    g.drawString(buf, 8, H - 20, 2);
    flush();
}

void Display::showInfo(const AppConfig& cfg, const String& ip, int batteryMv) {
    if (!spriteReady) createSprite();
    clear();
    auto& g = gfx();
    int W = screenW(), H = screenH();
    const int padX = 10;

    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_ACCENT, COLOR_BG);
    g.drawString("Device info", padX, 4, 2);
    g.setTextDatum(TR_DATUM);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("KEY: back", W - padX, 4, 1);
    g.drawFastHLine(0, 22, W, COLOR_DIVIDER);

    g.setTextDatum(TL_DATUM);
    int y = 28;

    // WiFi: inline (SSIDs are usually short)
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("WiFi:", padX, y, 2);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(cfg.ssid, padX + 52, y, 2);
    y += 24;

    // Auto-open 5h schedule: inline, local time (UTC+offset already baked in
    // at save time; we just present it back in what was the user's TZ then).
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("Auto 5h:", padX, y, 2);
    char ao[24];
    if (cfg.autoOpenEnabled) {
        int total = cfg.autoOpenHourUtc * 60 + cfg.autoOpenMinuteUtc
                  + cfg.autoOpenOffsetMin;
        int lh = ((total / 60) % 24 + 24) % 24;
        int lm = ((total % 60) + 60) % 60;
        snprintf(ao, sizeof(ao), "%02d:%02d", lh, lm);
        g.setTextColor(COLOR_TEXT, COLOR_BG);
    } else {
        snprintf(ao, sizeof(ao), "off");
    }
    g.drawString(ao, padX + 72, y, 2);
    y += 24;

    // Battery: "USBC (4.18V)" while plugged, "85% (3.94V)" on battery
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("Battery:", padX, y, 2);
    int bpct = Battery::percent(batteryMv);
    bool bchg = Battery::isCharging(batteryMv);
    char bb[32];
    if (bchg) {
        snprintf(bb, sizeof(bb), "USBC (%d.%02dV)",
                 batteryMv / 1000, (batteryMv / 10) % 100);
    } else {
        snprintf(bb, sizeof(bb), "%d%% (%d.%02dV)",
                 bpct, batteryMv / 1000, (batteryMv / 10) % 100);
    }
    uint16_t bc = bchg      ? COLOR_ACCENT
                : (bpct > 50) ? TFT_GREEN
                : (bpct > 20) ? TFT_YELLOW
                              : TFT_RED;
    g.setTextColor(bc, COLOR_BG);
    g.drawString(bb, padX + 72, y, 2);
    y += 24;

    // Helper: label on its own line, then wrapped value indented below.
    auto stacked = [&](const char* key, const String& value, uint16_t valueColor, int fontNum) {
        if (y >= H - 8) return;
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString(key, padX, y, 2);
        y += 16;
        g.setTextColor(valueColor, COLOR_BG);
        int charPx = (fontNum >= 2) ? 7 : 6;
        int lineH  = (fontNum >= 2) ? 14 : 10;
        int maxChars = max(8, (W - 2 * padX - 4) / charPx);
        for (int i = 0; i < (int)value.length() && y < H - lineH; i += maxChars) {
            g.drawString(value.substring(i, i + maxChars), padX + 4, y, fontNum);
            y += lineH;
        }
        y += 6;
    };

    stacked("Edit at:",    "http://" + ip,  COLOR_ACCENT, 2);
    stacked("orgId:",      cfg.orgId,       COLOR_TEXT,   1);
    stacked("sessionKey:", cfg.sessionKey,  COLOR_TEXT,   1);

    flush();
}

// ─── Stats panel (adaptive portrait / landscape) ────────────────────────────

static void drawBarAndMarker(TFT_eSPI& g, int barX, int barY, int barW, int barH,
                             const UsageWindow& w, const Pace& p) {
    g.fillRoundRect(barX, barY, barW, barH, 4, COLOR_BAR_BG);
    if (!w.valid) return;

    int usedW = (int)(p.used * barW + 0.5);
    if (usedW > 0) {
        if (usedW > barW) usedW = barW;
        g.fillRoundRect(barX, barY, usedW, barH, 4, p.color);
    }

    int mx = barX + (int)(p.elapsed * barW + 0.5);
    if (mx >= barX && mx <= barX + barW) {
        g.drawFastVLine(mx - 1, barY - 2, barH + 4, COLOR_BG);
        g.drawFastVLine(mx,     barY - 2, barH + 4, COLOR_MARKER);
        g.drawFastVLine(mx + 1, barY - 2, barH + 4, COLOR_BG);
    }
}

// Landscape panel: title+pace on one line, legend on one line.
static void drawPanelLandscape(TFT_eSPI& g, int y, const char* title,
                               const UsageWindow& w, const Pace& p,
                               time_t now, bool isWeekly, bool tall) {
    const int padX = 10;
    const int W    = g.width();

    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(title, padX, y + 2, 4);

    g.setTextDatum(TR_DATUM);
    if (w.valid) {
        g.setTextColor(p.color, COLOR_BG);
        g.drawString(p.label, W - padX, y + 6, 2);
    } else {
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString("no data", W - padX, y + 6, 2);
    }

    const int barY = y + (tall ? 46 : 34);
    const int barH = tall ? 20 : 14;
    drawBarAndMarker(g, padX, barY, W - 2 * padX, barH, w, p);

    const int legendY = barY + barH + 6;
    char buf[48];

    g.setTextDatum(TL_DATUM);
    g.setTextColor(w.valid ? p.color : COLOR_MUTED, COLOR_BG);
    if (w.valid) snprintf(buf, sizeof(buf), "used %d%%", (int)(p.used * 100 + 0.5));
    else         snprintf(buf, sizeof(buf), "used —");
    g.drawString(buf, padX, legendY, 2);

    g.setTextColor(COLOR_MUTED, COLOR_BG);
    if (w.valid) snprintf(buf, sizeof(buf), "elapsed %d%%", (int)(p.elapsed * 100 + 0.5));
    else         snprintf(buf, sizeof(buf), "elapsed —");
    g.drawString(buf, padX + 90, legendY, 2);

    g.setTextDatum(TR_DATUM);
    if (w.valid && w.resetsAt > 0) {
        String s = "Resets " + Stats::formatCountdown(w.resetsAt, now, isWeekly);
        g.drawString(s, W - padX, legendY, 2);
    }
}

// Portrait panel: title top, pace label below, then bar, then stacked legend.
static void drawPanelPortrait(TFT_eSPI& g, int y, int panelH, const char* title,
                              const UsageWindow& w, const Pace& p,
                              time_t now, bool isWeekly, bool tall) {
    const int padX = 10;
    const int W    = g.width();

    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(title, padX, y + 2, 4);

    int paceY = y + 30;
    if (w.valid) {
        g.setTextColor(p.color, COLOR_BG);
        g.drawString(p.label, padX, paceY, 2);
    } else {
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString("no data", padX, paceY, 2);
    }

    int barY = paceY + 22;
    int barH = tall ? 20 : 16;
    drawBarAndMarker(g, padX, barY, W - 2 * padX, barH, w, p);

    int legendY = barY + barH + 8;
    char buf[48];

    g.setTextColor(w.valid ? p.color : COLOR_MUTED, COLOR_BG);
    if (w.valid) snprintf(buf, sizeof(buf), "used %d%%", (int)(p.used * 100 + 0.5));
    else         snprintf(buf, sizeof(buf), "used —");
    g.drawString(buf, padX, legendY, 2);
    legendY += 16;

    g.setTextColor(COLOR_MUTED, COLOR_BG);
    if (w.valid) snprintf(buf, sizeof(buf), "elapsed %d%%", (int)(p.elapsed * 100 + 0.5));
    else         snprintf(buf, sizeof(buf), "elapsed —");
    g.drawString(buf, padX, legendY, 2);
    legendY += 16;

    if (w.valid && w.resetsAt > 0) {
        String s = "Resets " + Stats::formatCountdown(w.resetsAt, now, isWeekly);
        g.drawString(s, padX, legendY, 2);
    }
}

static void drawPanel(TFT_eSPI& g, int y, int panelH, const char* title,
                      const UsageWindow& w, const Pace& p,
                      time_t now, bool isWeekly, bool tall) {
    if (g.width() < g.height()) drawPanelPortrait(g, y, panelH, title, w, p, now, isWeekly, tall);
    else                        drawPanelLandscape(g, y, title,          w, p, now, isWeekly, tall);
}

void Display::showStats(const Usage& usage, time_t now, int batteryMv,
                         int refreshInSec, bool stale) {
    if (!spriteReady) createSprite();
    clear();
    auto& g = gfx();
    int W = screenW(), H = screenH();
    const bool landscape = W > H;

    // Header
    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_ACCENT, COLOR_BG);
    g.drawString("ClaudeStats", 8, 4, 2);

    // Battery indicator (top-right): "NN%" + icon
    const int iconW = 22, iconH = 10, nubW = 2, nubH = 4;
    const int rightX = W - 8;
    const int iconX  = rightX - iconW - nubW;
    const int iconY  = 7;

    int pct = Battery::percent(batteryMv);
    bool charging = Battery::isCharging(batteryMv);
    uint16_t fillColor = (pct > 50) ? TFT_GREEN
                       : (pct > 20) ? TFT_YELLOW
                                    : TFT_RED;
    uint16_t borderColor = charging ? COLOR_ACCENT : COLOR_MUTED;

    g.drawRect(iconX, iconY, iconW, iconH, borderColor);
    g.fillRect(iconX + iconW, iconY + (iconH - nubH) / 2, nubW, nubH, borderColor);
    int fillW = ((iconW - 2) * pct) / 100;
    if (fillW > 0) g.fillRect(iconX + 1, iconY + 1, fillW, iconH - 2, fillColor);

    char pctBuf[8];
    if (charging) snprintf(pctBuf, sizeof(pctBuf), "USBC");
    else          snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    g.setTextDatum(TR_DATUM);
    g.setTextColor(charging ? COLOR_ACCENT : COLOR_MUTED, COLOR_BG);
    g.drawString(pctBuf, iconX - 4, 4, 2);
    int pctW = g.textWidth(pctBuf, 2);

    // Landscape has room for the refresh countdown to the left of the battery.
    int refreshRightX = iconX - pctW - 12;
    if (landscape) {
        char rbuf[24];
        snprintf(rbuf, sizeof(rbuf), "refresh %ds", refreshInSec);
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString(rbuf, refreshRightX, 4, 2);
        refreshRightX -= g.textWidth(rbuf, 2) + 8;
    }

    if (stale) {
        g.setTextColor(TFT_RED, COLOR_BG);
        g.drawString("CACHED", refreshRightX, 4, 2);
    }

    g.drawFastHLine(0, 22, W, COLOR_DIVIDER);

    Pace p5 = Stats::computePace(usage.fiveHour, 5L * 3600,       now);
    Pace p7 = Stats::computePace(usage.sevenDay, 7L * 24 * 3600,  now);

    const bool has5 = usage.fiveHour.valid;
    const bool has7 = usage.sevenDay.valid;

    int contentY = 24;
    int contentH = H - contentY;

    const bool portrait = W < H;
    const char* t5 = portrait ? "5-hour" : "5-hour window";
    const char* t7 = "Weekly";

    if (!has5 && !has7) {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString("No usage data", W / 2, H / 2 + 8, 4);
    } else if (has5 && has7) {
        int panelH = (contentH - 2) / 2;
        drawPanel(g, contentY,               panelH, t5, usage.fiveHour, p5, now, false, false);
        g.drawFastHLine(0, contentY + panelH, W, COLOR_DIVIDER);
        drawPanel(g, contentY + panelH + 2,  panelH, t7, usage.sevenDay, p7, now, true,  false);
    } else if (has7) {
        drawPanel(g, contentY + 14, contentH - 14, t7, usage.sevenDay, p7, now, true,  true);
    } else {
        drawPanel(g, contentY + 14, contentH - 14, t5, usage.fiveHour, p5, now, false, true);
    }

    flush();
}
