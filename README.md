# ClaudeStatsPortable

A portable, stand-alone version of [ClaudeStats](https://github.com/alberthorta/ClaudeStats) that runs on a **LilyGo T-Display S3** ESP32 board and shows your Claude Code subscription usage on a 1.9" color display — no computer required.

Displays the **5-hour window** and **weekly window** utilization with the same pace-aware visualization as the macOS app: a colored progress bar fills according to how much of the window you've used, and a white marker shows where you *should* be based on elapsed time. If the fill is left of the marker you're cruising; if it's right, you're burning fast.

---

## Hardware

- **Board**: [LilyGo T-Display S3](https://www.lilygo.cc/products/t-display-s3) (ESP32-S3, 16 MB flash, 8 MB PSRAM)
- **Display**: 1.9" ST7789 IPS, 170×320 pixels, parallel 8-bit interface
- **Buttons**: BOOT (GPIO0) and KEY (GPIO14)
- **Connectivity**: WiFi 2.4 GHz
- **Power**: USB-C

The firmware uses the non-Touch variant; the Touch version should also work but its capacitive layer is not used.

---

## Features

- **Fully autonomous** — connects directly to `claude.ai/api/organizations/{orgId}/usage` over HTTPS, no companion app required
- **5-hour and weekly utilization panels** with pace bar, pace label (`Well under pace`, `Under pace`, `On pace`, `Over pace`, `Burning fast`), used/elapsed percentages, and countdown to reset
- **Colors mirror the macOS app** (green / mint / yellow / orange / red) based on `used / elapsed`
- **Adaptive layout** — if only one window has data the panel expands to full screen
- **Rotatable screen** — BOOT button cycles through 4 orientations (landscape / portrait / flipped), layout adapts automatically. The chosen rotation persists across reboots.
- **Resilient to transient errors** — if a fetch fails, the previous values stay on screen with a small red **CACHED** badge next to the refresh counter. The full-screen error view only appears on the very first boot when no data has been fetched yet.
- **Captive portal provisioning** — on first boot the device creates an open `ClaudeStats` WiFi AP; connect to it and any browser request is redirected to the setup form
- **Auto-discovery of `orgId`** — only the `sessionKey` is required, the organization UUID is fetched from `/api/organizations` and cached in NVS
- **On-device web config** — short-press KEY shows the device IP, visit it from any browser to edit settings, reboot is automatic after save
- **WiFi-only reset** — long-press KEY (10 s) wipes WiFi credentials but keeps the `sessionKey` and `orgId`, so you only need to re-enter WiFi when you change location
- **ClaudeStats-style web UI** — mesh-gradient dark theme, glass card, SVG logo, works offline in AP mode

---

## Screens

1. **Setup mode** (first boot or after WiFi reset): "Setup mode" banner with AP SSID and `http://192.168.4.1`.
2. **Connecting**: while joining the stored WiFi.
3. **Stats** (main view): two stacked panels for 5 h and weekly. Header shows `ClaudeStats` and `refresh Ns` countdown.
4. **Info** (short-press KEY): WiFi name, device IP, organization UUID, full `sessionKey`, and a hint to the edit URL.
5. **Reset**: brief "Clearing config..." confirmation when the 10 s hold fires.
6. **Errors**: WiFi connect failure, session expired, network or parse errors.

---

## Controls

| Action | Result |
|---|---|
| Short press **KEY** (GPIO14) | Toggle between Stats and Info views |
| Long press **KEY** (≥ 10 s) | Clear WiFi credentials, reboot into setup mode. **Keeps `sessionKey` and `orgId`** so you only re-enter the WiFi. |
| Short press **BOOT** (GPIO0) | Rotate the screen 90° (cycles through 4 orientations, saved in NVS) |
| Open `http://<device-ip>` | Edit config form with WiFi + sessionKey, reboots on save |

---

## Installation

### Prerequisites

- macOS / Linux / Windows with [PlatformIO Core](https://platformio.org/install/cli) installed
  ```
  brew install platformio        # macOS
  pip install -U platformio      # any OS with Python
  ```
- A LilyGo T-Display S3, connected by USB-C

### Build and flash

```bash
git clone https://github.com/alberthorta/ClaudeStatsPortable.git
cd ClaudeStatsPortable
pio run -t upload
```

On first build PlatformIO downloads the ESP32 platform, toolchain, TFT_eSPI and ArduinoJson (~2–3 minutes). Subsequent builds take ~20 s.

To stream serial logs:
```bash
pio device monitor -b 115200
```

---

## Configuration

### First-time setup

1. Power the device. It boots into **Setup mode** and exposes an open WiFi network named **`ClaudeStats`**.
2. Connect your phone or laptop to that network. A captive-portal dialog should open automatically; if not, browse to `http://192.168.4.1`.
3. Fill the form:
   - **WiFi network** — picked from the scan
   - **WiFi password**
   - **Claude sessionKey** — see below
4. The device validates WiFi + internet access, stores everything in NVS, discovers the organization UUID from `/api/organizations`, and reboots into the stats view.

### Getting the `sessionKey`

1. Sign in to [claude.ai](https://claude.ai) in your browser.
2. Open DevTools (⌥⌘I) → **Application** (Chrome) / **Storage** (Firefox/Safari) → **Cookies** → `https://claude.ai`.
3. Copy the value of the `sessionKey` cookie (starts with `sk-ant-sid01-…`).

### Re-configuration

From any browser on the same WiFi, visit `http://<device-ip>`. The IP is shown on the **Info** screen (short-press KEY). The form prefills with current values. Saving reboots the device and re-discovers the organization.

---

## Architecture

```
src/
├── main.cpp          — boot flow, button handling, refresh loop
├── config.{h,cpp}    — NVS (Preferences) load / save / clear / clearWifi
├── display.{h,cpp}   — TFT_eSPI + off-screen sprite; all screens
├── provisioning.{h,cpp} — SoftAP + DNS hijack + captive portal + setup form
├── webconfig.{h,cpp} — HTTP server on STA IP for re-configuration
├── api.{h,cpp}       — HTTPS client (WiFiClientSecure + HTTPClient + ArduinoJson)
├── stats.{h,cpp}     — pace calculation and countdown formatting
└── webui.{h,cpp}     — shared inline CSS + SVG logo for the web pages
```

### Data flow

1. On boot, `Config::load()` reads the four NVS keys (`ssid`, `password`, `sessionKey`, `orgId`). If WiFi or sessionKey is missing, `Provisioning::run()` starts — SoftAP, DNS server responding every query with `192.168.4.1`, and an `HTTP` form that validates + saves + reboots.
2. With valid config, the device joins WiFi (`WIFI_STA`), syncs time via NTP (UTC), and — if `orgId` is empty — calls `GET /api/organizations` with the session cookie to pick the first org.
3. Every 30 s it calls `GET https://claude.ai/api/organizations/{orgId}/usage` and parses the `five_hour` / `seven_day` windows. Each window has a `utilization` (0–100) and an ISO-8601 `resets_at`.
4. `Stats::computePace()` derives `used = utilization/100`, `elapsed = 1 - remaining/total`, `ratio = used/elapsed`, and maps to one of five colors/labels (same thresholds as the Swift app: 0.75 / 0.95 / 1.10 / 1.35).
5. The UI redraws once per second so the countdowns update smoothly. The stats view and info view are drawn to an off-screen 16-bit sprite in PSRAM and pushed with `pushSprite()` to avoid flicker.
6. A short button press toggles between stats and info views. A 10 s hold calls `Config::clearWifi()` — which removes only `ssid`/`password`, preserving `sessionKey` and `orgId` — and reboots.

### HTTPS

The firmware uses `WiFiClientSecure::setInsecure()`. TLS certificate validation is disabled to avoid bundling and maintaining a root CA store on the device. This is acceptable for a personal device on a trusted network; if you want strict validation, replace with `setCACert()` and ship the relevant root (Cloudflare fronts `claude.ai`, so Baltimore CyberTrust or ISRG Root X1 work).

### Storage

All configuration is kept in the ESP32 NVS under the `claudestats` namespace. Flash wear is negligible (a single write per save).

---

## Troubleshooting

- **Captive portal doesn't open** — just browse to `http://192.168.4.1` manually; some browsers cache old 204 responses.
- **`Invalid sessionKey`** at boot — your cookie rotated. Visit `http://<device-ip>` and paste a fresh value.
- **Stats show `No usage data`** — your plan doesn't expose 5 h / 7 d windows at the moment, or you haven't used Claude recently.
- **Stuck in "Connecting"** — the device reboots automatically after 20 s of WiFi timeout; long-press KEY to wipe and re-enter credentials.
- **Compile error about TFT_eSPI pins** — the `platformio.ini` sets every TFT_eSPI flag as a `-D` build flag. Don't edit `User_Setup.h` inside the library; let the flags drive it.

---

## Related projects

- **[ClaudeStats](https://github.com/alberthorta/ClaudeStats)** — the macOS menu bar app this project ports to hardware.
- **[she-llac/claude-counter](https://github.com/she-llac/claude-counter)** — credit for discovering the `/api/organizations/{orgId}/usage` endpoint.

---

## License

MIT — see [LICENSE](LICENSE).
