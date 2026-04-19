#include "display.h"
#include <TFT_eSPI.h>

static TFT_eSPI      tft;
static TFT_eSprite   sprite = TFT_eSprite(&tft);
static bool          spriteReady = false;

static const int SCREEN_W = 320;
static const int SCREEN_H = 170;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const uint16_t COLOR_BG        = rgb565(  8,  10,  16);  // near black
static const uint16_t COLOR_PANEL_BG  = rgb565( 16,  20,  28);
static const uint16_t COLOR_DIVIDER   = rgb565( 40,  44,  56);
static const uint16_t COLOR_TEXT      = TFT_WHITE;
static const uint16_t COLOR_MUTED     = rgb565(140, 146, 160);
static const uint16_t COLOR_BAR_BG    = rgb565( 32,  36,  46);
static const uint16_t COLOR_MARKER    = TFT_WHITE;
static const uint16_t COLOR_ACCENT    = rgb565( 90, 170, 255);

// ─── Helpers ────────────────────────────────────────────────────────────────

static void ensureSprite() {
    if (spriteReady) return;
    sprite.setColorDepth(16);
    sprite.createSprite(SCREEN_W, SCREEN_H);
    spriteReady = sprite.created();
}

static void flush() {
    if (spriteReady) sprite.pushSprite(0, 0);
}

// Drawing target: use sprite when available, else direct to tft.
static TFT_eSPI& gfx() {
    return spriteReady ? (TFT_eSPI&)sprite : tft;
}

static void clear(uint16_t color = COLOR_BG) {
    if (spriteReady) sprite.fillSprite(color);
    else tft.fillScreen(color);
}

// ─── Full-screen single-message helpers (no sprite needed) ──────────────────

static void simpleHeader(const char* title, uint16_t color = TFT_CYAN) {
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, COLOR_BG);
    tft.drawString(title, 8, 8, 4);
    tft.drawFastHLine(0, 34, SCREEN_W, COLOR_DIVIDER);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
}

void Display::begin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    ensureSprite();
}

void Display::showProvisioning(const String& apSsid, const String& ip) {
    spriteReady = false; // skip sprite for simple screens
    simpleHeader("Setup mode");
    tft.setCursor(8, 46, 2);
    tft.printf("Connect to WiFi:\n  %s\n\n", apSsid.c_str());
    tft.printf("Open browser at:\n  http://%s", ip.c_str());
    ensureSprite();
}

void Display::showConnecting(const String& ssid) {
    spriteReady = false;
    simpleHeader("Connecting");
    tft.setCursor(8, 56, 2);
    tft.printf("SSID: %s", ssid.c_str());
    ensureSprite();
}

void Display::showResetting() {
    spriteReady = false;
    simpleHeader("Reset", TFT_ORANGE);
    tft.setCursor(8, 56, 2);
    tft.print("Clearing config...");
    ensureSprite();
}

void Display::showLoading(const String& msg) {
    ensureSprite();
    clear();
    auto& g = gfx();
    g.setTextDatum(MC_DATUM);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString("ClaudeStats", SCREEN_W/2, SCREEN_H/2 - 14, 4);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString(msg, SCREEN_W/2, SCREEN_H/2 + 14, 2);
    flush();
}

void Display::showApiError(const String& msg, int refreshInSec) {
    ensureSprite();
    clear();
    auto& g = gfx();
    g.setTextDatum(TL_DATUM);
    g.setTextColor(TFT_RED, COLOR_BG);
    g.drawString("API error", 8, 8, 4);
    g.drawFastHLine(0, 36, SCREEN_W, COLOR_DIVIDER);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(msg, 8, 48, 2);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    char buf[32];
    snprintf(buf, sizeof(buf), "Retry in %ds", refreshInSec);
    g.drawString(buf, 8, SCREEN_H - 20, 2);
    flush();
}

// ─── Stats panel ────────────────────────────────────────────────────────────

static void drawPanel(TFT_eSPI& g, int y, int h, const char* title,
                      const UsageWindow& w, const Pace& p,
                      time_t now, bool isWeekly, bool tall) {
    const int padX = 10;

    // Title (left)
    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(title, padX, y + 2, 4);

    // Pace label (right, colored). If no data, show em-dash.
    g.setTextDatum(TR_DATUM);
    if (w.valid) {
        g.setTextColor(p.color, COLOR_BG);
        g.drawString(p.label, SCREEN_W - padX, y + 6, 2);
    } else {
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString("no data", SCREEN_W - padX, y + 6, 2);
    }

    // Progress bar (thicker when panel is tall/solo)
    const int barY = y + (tall ? 46 : 34);
    const int barH = tall ? 20 : 14;
    const int barX = padX;
    const int barW = SCREEN_W - 2 * padX;
    g.fillRoundRect(barX, barY, barW, barH, 4, COLOR_BAR_BG);

    if (w.valid) {
        int usedW = (int)(p.used * barW + 0.5);
        if (usedW > 0) {
            if (usedW > barW) usedW = barW;
            g.fillRoundRect(barX, barY, usedW, barH, 4, p.color);
        }

        // Elapsed marker (thin vertical white line with dark outline)
        int mx = barX + (int)(p.elapsed * barW + 0.5);
        if (mx >= barX && mx <= barX + barW) {
            g.drawFastVLine(mx - 1, barY - 2, barH + 4, COLOR_BG);
            g.drawFastVLine(mx,     barY - 2, barH + 4, COLOR_MARKER);
            g.drawFastVLine(mx + 1, barY - 2, barH + 4, COLOR_BG);
        }
    }

    // Legend row
    const int legendY = barY + barH + 6;
    char buf[48];

    g.setTextDatum(TL_DATUM);
    if (w.valid) {
        g.setTextColor(p.color, COLOR_BG);
        snprintf(buf, sizeof(buf), "used %d%%", (int)(p.used * 100 + 0.5));
    } else {
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        snprintf(buf, sizeof(buf), "used —");
    }
    g.drawString(buf, padX, legendY, 2);

    g.setTextColor(COLOR_MUTED, COLOR_BG);
    if (w.valid) {
        snprintf(buf, sizeof(buf), "elapsed %d%%", (int)(p.elapsed * 100 + 0.5));
    } else {
        snprintf(buf, sizeof(buf), "elapsed —");
    }
    g.drawString(buf, padX + 90, legendY, 2);

    g.setTextDatum(TR_DATUM);
    if (w.valid && w.resetsAt > 0) {
        String s = "Resets " + Stats::formatCountdown(w.resetsAt, now, isWeekly);
        g.drawString(s, SCREEN_W - padX, legendY, 2);
    }
}

void Display::showInfo(const AppConfig& cfg, const String& ip) {
    ensureSprite();
    clear();
    auto& g = gfx();

    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_ACCENT, COLOR_BG);
    g.drawString("Device info", 8, 4, 2);
    g.setTextDatum(TR_DATUM);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("KEY: back   hold 10s: reset", SCREEN_W - 8, 4, 1);
    g.drawFastHLine(0, 22, SCREEN_W, COLOR_DIVIDER);

    const int padX = 10;
    int y = 28;

    // WiFi + IP
    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("WiFi:", padX, y, 2);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(cfg.ssid, padX + 54, y, 2);
    y += 18;

    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("Edit:", padX, y, 2);
    g.setTextColor(COLOR_ACCENT, COLOR_BG);
    g.drawString("http://" + ip, padX + 54, y, 2);
    y += 22;

    // orgId (single line)
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("orgId:", padX, y, 2);
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    g.drawString(cfg.orgId, padX + 54, y, 2);
    y += 20;

    // sessionKey (wrap)
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    g.drawString("sessionKey:", padX, y, 2);
    y += 14;
    g.setTextColor(COLOR_TEXT, COLOR_BG);
    const int lineLen = 52;  // GLCD font 1 (6px) fits ~52 chars in 312px
    for (int i = 0; i < (int)cfg.sessionKey.length() && y < SCREEN_H - 8; i += lineLen) {
        String chunk = cfg.sessionKey.substring(i, i + lineLen);
        g.drawString(chunk, padX, y, 1);
        y += 10;
    }

    flush();
}

void Display::showStats(const Usage& usage, time_t now, int refreshInSec) {
    ensureSprite();
    clear();
    auto& g = gfx();

    // Header strip
    g.setTextDatum(TL_DATUM);
    g.setTextColor(COLOR_ACCENT, COLOR_BG);
    g.drawString("ClaudeStats", 8, 4, 2);

    g.setTextDatum(TR_DATUM);
    g.setTextColor(COLOR_MUTED, COLOR_BG);
    char buf[24];
    snprintf(buf, sizeof(buf), "refresh %ds", refreshInSec);
    g.drawString(buf, SCREEN_W - 8, 4, 2);

    g.drawFastHLine(0, 22, SCREEN_W, COLOR_DIVIDER);

    Pace p5 = Stats::computePace(usage.fiveHour, 5L * 3600,       now);
    Pace p7 = Stats::computePace(usage.sevenDay, 7L * 24 * 3600,  now);

    const bool has5 = usage.fiveHour.valid;
    const bool has7 = usage.sevenDay.valid;

    if (!has5 && !has7) {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(COLOR_MUTED, COLOR_BG);
        g.drawString("No usage data", SCREEN_W / 2, SCREEN_H / 2 + 8, 4);
    } else if (has5 && has7) {
        drawPanel(g, 24,  72, "5-hour window", usage.fiveHour, p5, now, false, false);
        g.drawFastHLine(0, 96, SCREEN_W, COLOR_DIVIDER);
        drawPanel(g, 98,  72, "Weekly",        usage.sevenDay, p7, now, true,  false);
    } else if (has7) {
        drawPanel(g, 38, 132, "Weekly",        usage.sevenDay, p7, now, true,  true);
    } else {
        drawPanel(g, 38, 132, "5-hour window", usage.fiveHour, p5, now, false, true);
    }

    flush();
}
