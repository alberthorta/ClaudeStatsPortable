# ClaudeStatsPortable

A portable, stand-alone version of [ClaudeStats](https://github.com/alberthorta/ClaudeStats) that runs on a **LilyGo T-Display S3** ESP32 board and shows your Claude Code subscription usage on a 1.9" color display — no computer required.

Displays the **5-hour window** and **weekly window** utilization with the same pace-aware visualization as the macOS app: a colored progress bar fills according to how much of the window you've used, and a white marker shows where you *should* be based on elapsed time. If the fill is left of the marker you're cruising; if it's right, you're burning fast.

The device is **battery-capable**, with three tiers of sleep for long standby, an optional **daily auto-trigger** of a new 5-hour window at a chosen time, a password-protected **web admin** for configuration, and a full CRUD list of known WiFi networks it can roam between.

---

## Hardware

- **Board**: [LilyGo T-Display S3](https://www.lilygo.cc/products/t-display-s3) (ESP32-S3, 16 MB flash, 8 MB PSRAM)
- **Display**: 1.9" ST7789 IPS, 170×320 pixels, parallel 8-bit interface
- **Buttons**: BOOT (GPIO0) and KEY (GPIO14)
- **Connectivity**: WiFi 2.4 GHz
- **Power**: USB-C and/or LiPo battery via the board's JST connector (built-in TP4054 charger at ~500 mA)
- **Battery monitoring**: voltage on GPIO4 through the board's 2:1 divider
- **Display power**: GPIO15 drives the LCD power rail — the firmware holds it HIGH so the screen stays on when USB is unplugged

Tested with two 500 mAh LiPo cells in parallel (1000 mAh total), giving ~12 h of active use and up to ~10 days with the idle sleep tiers enabled.

---

## Features

### Display

- Two stacked panels (5 h and weekly) with pace bar, pace label (`Well under pace`, `Under pace`, `On pace`, `Over pace`, `Burning fast`), used / elapsed percentages and countdown to reset.
- Colors mirror the macOS app (green / mint / yellow / orange / red) based on `used / elapsed`.
- Adaptive layout — if only one window has data, its panel expands to full screen.
- Battery icon + percent in the top-right corner, color-coded by level (green / yellow / red). On USB the label swaps to `USBC` with a cyan border.
- `refresh Ns` countdown next to the battery in landscape (portrait has no room for it).
- `CACHED` red badge if the last fetch failed — previous values stay on screen instead of a full-screen error.
- 4 orientations (BOOT button cycles through them), persistent across reboots.

### Data & scheduling

- Fully autonomous — speaks directly to `claude.ai/api/organizations/{orgId}/usage` over HTTPS. No Mac, phone or bridge.
- `orgId` is auto-discovered from the session cookie and cached in NVS.
- **Daily 5 h-window auto-open** (opt-in, off by default): at a chosen time each day the device sends one minimal message so a fresh 5-hour window starts exactly when you want it. Useful if you're a burst-limited user who wants the window aligned with your workday.

### Power management — three-tier sleep

- **Tier 1 — screen off** (hold BOOT 2 s → cancellable 3 s countdown): backlight off, everything else running, fetches continue. Any button wakes.
- **Tier 2 — light sleep** (automatic after *N* minutes of tier 1): CPU paused, WiFi keeps the association, ~1–10 mA. Wakes every 30 s for a silent background fetch; any button wake triggers a clean reboot (~5 s) so the device always comes back in a known state.
- **Tier 3 — deep sleep / drawer mode** (hold BOOT + KEY 5 s → cancellable 5 s countdown): CPU off, ~0.3–0.5 mA. If daily auto-open is enabled, the RTC wakes the device at the trigger time, fires it silently, and goes back to sleep. Any button wake = full cold boot.
- Each transition has a visible countdown; any button press during the countdown cancels back to normal operation.
- On wake from tier 1 the device forces a fresh fetch so the first frame isn't stale.

### WiFi management

- Saved WiFi credentials are kept as a prioritized list (up to 8). At boot the device scans the air, cross-references against the list and connects to the **first visible entry in priority order**.
- The list is editable in the admin UI: add, delete, and reorder (↑ / ↓) without a reboot.
- The legacy primary SSID is auto-migrated into the list on first run after an upgrade.

### Web admin

- Password-protected (HTTP Basic, default **admin / admin**).
- Live battery status, currently-connected SSID, MAC address.
- Saved WiFi CRUD with priority reorder.
- `sessionKey` editor.
- Daily auto-open toggle + time picker in your local timezone (converted to UTC automatically — DST reflected every time you save).
- Idle light-sleep timeout in minutes (0 disables tier 2).
- Admin username / password editor (empty password keeps the current).
- AJAX save with an inline status banner — the page doesn't navigate away, just tells you the device is rebooting and waits for reload.
- A refresh button to re-poll the device status from the browser.

### Provisioning

- Captive portal on first boot: the device creates an open `ClaudeStats` WiFi AP; any HTTP request on the AP is redirected to the setup form.
- Same mesh-gradient, glass-card UI as the main admin page. Works entirely offline, no CDNs.

---

## Controls

| Action | Result |
|---|---|
| Short press **KEY** | Toggle between Stats and Info views. |
| Hold **KEY** ≥ 10 s | Armed reboot countdown (5 s). KEY again = cancel. **Hold BOOT 2 s during the countdown** = factory reset (wipes every NVS key → admin UI goes back to admin/admin, WiFi list emptied, auto-open cleared, forces re-provisioning). |
| Short press **BOOT** | Rotate the screen 90° (cycles through 4 orientations, saved). |
| Hold **BOOT** 2 s | Armed "Screen off in 3…" countdown. Any button cancels. At 0 → tier 1 (backlight off). |
| Hold **BOOT + KEY** together 5 s | Armed "Deep sleep in 5…" countdown. Any button (after releasing the combo) cancels. At 0 → tier 3 (drawer mode). |
| Any button while asleep | Wake. Tier 1 wake is instant + refreshes stats; tier 2 and tier 3 wakes trigger a clean reboot (~5 s). |
| Open `http://<device-ip>` | Admin web UI (auth required). |

---

## Screens

1. **Setup mode** — on first boot or after a factory reset. Banner with the AP SSID and `http://192.168.4.1`.
2. **Scanning WiFi / Connecting** — while the device walks its priority list.
3. **Stats** (main view) — 5 h and weekly panels, battery icon + percent (or `USBC`), `refresh Ns` countdown in landscape, `CACHED` badge on fetch failure.
4. **Info** (short-press KEY) — WiFi name, device IP, auto-open schedule in local time, battery percent + voltage, `orgId`, full `sessionKey`, edit URL.
5. **Screen off / light sleep / deep sleep countdowns** — centered big orange digit with a "press KEY / any button to cancel" hint.
6. **Reboot countdown** — same visual, plus "KEY cancel · BOOT reset" hint.
7. **Factory reset sub-countdown** — shown while BOOT is held during the reboot countdown.
8. **Errors** — WiFi connect failure, invalid sessionKey, network / parse errors. Data never disappears on a single bad fetch; the `CACHED` badge covers that case.

---

## Installation

### Prerequisites

- macOS / Linux / Windows with [PlatformIO Core](https://platformio.org/install/cli)
  ```
  brew install platformio        # macOS
  pip install -U platformio      # any OS with Python
  ```
- A LilyGo T-Display S3 connected by USB-C.

### Build and flash

```bash
git clone https://github.com/alberthorta/ClaudeStatsPortable.git
cd ClaudeStatsPortable
pio run -t upload
```

First build pulls the ESP32 platform, toolchain, TFT_eSPI and ArduinoJson (~2–3 minutes). Incremental builds take ~20 s. Flashing takes ~10 s at 921600 baud.

To stream serial logs:
```bash
pio device monitor -b 115200
```

---

## Configuration

### First-time setup

1. Power the device. It boots into **Setup mode** and exposes an open WiFi network named **`ClaudeStats`**.
2. Connect your phone or laptop to that network. A captive-portal dialog should open automatically; otherwise browse to `http://192.168.4.1`.
3. Fill the form:
   - **WiFi network** — picked from the scan
   - **WiFi password**
   - **Claude sessionKey** — see below
4. The device validates WiFi + internet access, saves to NVS, discovers the organization UUID from `/api/organizations`, and reboots into the stats view.

### Getting the `sessionKey`

1. Sign in to [claude.ai](https://claude.ai) in your browser.
2. Open DevTools (⌥⌘I) → **Application** (Chrome) / **Storage** (Firefox/Safari) → **Cookies** → `https://claude.ai`.
3. Copy the value of the `sessionKey` cookie (starts with `sk-ant-sid01-…`).

### Admin web UI

From any browser on the same WiFi, visit `http://<device-ip>` (the IP is shown on the Info screen, short-press KEY).

- **Authentication**: HTTP Basic, default **admin / admin**. Change it in the admin form (empty password field = keep current).
- **Live status**: battery voltage + percent, currently-connected SSID, MAC address.
- **Saved WiFi networks**: table with ↑ / ↓ priority arrows and Delete per row, plus an add form (SSID + password). Changes take effect immediately, no reboot for CRUD operations.
- **Other settings** (saving this section reboots the device):
  - `sessionKey`
  - **Auto-open 5 h window daily** — checkbox + time picker in local time
  - **Idle light sleep** — minutes, 0 disables (default 5)
  - **Admin username / password**
- **Refresh button** and **AJAX save** — the page doesn't navigate away on save; an inline banner tells you the device is rebooting.

### Daily 5-hour-window auto-open

Enable the checkbox and pick the hour you'd like a fresh 5-hour window to start. At that time every day the device sends a minimal throwaway message to your account, which registers as a "first message" and starts the 5-hour clock. The conversation is deleted automatically afterwards, so nothing clutters your chat sidebar.

This is useful if you routinely hit the 5-hour burst cap before hitting the weekly cap: aligning the window with the start of your work block gives you a full five hours of headroom at exactly the right moment. It is **off by default** and costs one message per day against the weekly bucket.

### Idle sleep timeout

`Idle light sleep` in the admin form sets how long tier 1 (backlight off) has to be idle before the device drops into tier 2 (light sleep). 0 disables auto light sleep, 1–60 minutes otherwise. The transition shows a 3 s cancellable countdown; once in tier 2 the only way out is a button press (which reboots).

### Factory reset

Hold **KEY for 10 s** → a 5 s armed reboot countdown appears. During those 5 s, **hold BOOT for 2 s** and the device wipes every NVS key and reboots into provisioning. This clears:

- WiFi list
- `sessionKey` and `orgId`
- Auto-open schedule
- Idle sleep minutes
- Screen rotation
- Admin username and password (back to `admin` / `admin`)

---

## Power and battery

With USB-C connected, the board charges the battery through its on-board TP4054 (max ~500 mA). On battery:

| State | Typical draw | Autonomy on 1000 mAh |
|---|---|---|
| Stats view active | ~60–100 mA | ~10–17 h |
| Tier 1 — backlight off | ~40–60 mA | ~17–25 h |
| Tier 2 — light sleep (95 % idle) | ~5 mA average | ~8–10 days |
| Tier 3 — deep sleep | ~0.3–0.5 mA | ~80 days |

The battery indicator uses a piecewise-linear LiPo discharge curve (4.20 V = 100 %, 3.30 V = 0 %). When USB is plugged in and holding the rail above ~4.15 V, the label swaps to `USBC` and the border colors turn cyan.

---

## Architecture

```
src/
├── main.cpp          — boot flow, button state machines, refresh loop, sleep tiers, scheduler
├── config.{h,cpp}    — NVS (Preferences) load/save/clear + WiFi-list helpers
├── display.{h,cpp}   — TFT_eSPI + off-screen sprite; all screens, panel sleep-in/out
├── battery.{h,cpp}   — ADC read of GPIO4, LiPo percent curve, charging heuristic
├── provisioning.{h,cpp} — SoftAP + DNS hijack + captive portal setup form
├── webconfig.{h,cpp} — authenticated HTTP admin on the STA IP
├── api.{h,cpp}       — HTTPS client for /usage, /organizations, and /chat_conversations (auto-open)
├── stats.{h,cpp}     — pace calculation + countdown formatting
└── webui.{h,cpp}     — shared inline CSS + SVG logo for provisioning and admin
```

### Data flow

1. **Boot**: `Config::load()` pulls everything from NVS (WiFi list, sessionKey, orgId, auto-open schedule, idle minutes, admin credentials, rotation, last-auto-open date). If config is missing, `Provisioning::run()` starts (SoftAP + captive portal).
2. **Wakeup cause dispatch**: if `esp_sleep_get_wakeup_cause()` is `TIMER`, the device is cold-booting from tier 3 to fire the daily auto-open — it connects WiFi silently, calls `Api::openWindow`, marks the day done and re-enters deep sleep.
3. **WiFi**: `connectAnyKnown()` scans the air, intersects with the saved list, and tries entries in priority order (not by RSSI).
4. **NTP** is synced to UTC via `configTime(0, 0, …)` so `resets_at` parsing stays in UTC throughout.
5. **Usage fetch** every 30 s: `GET /api/organizations/{orgId}/usage` with the `sessionKey` cookie, parsing the `five_hour` and `seven_day` windows.
6. **Pace**: `Stats::computePace()` derives `used = utilization/100`, `elapsed = 1 - remaining/total`, `ratio = used/elapsed`, and maps to colors / labels at 0.75 / 0.95 / 1.10 / 1.35 (same thresholds as the Swift app).
7. **Render loop**: 1 Hz ticks into a 16-bit PSRAM sprite, pushed with `pushSprite` to avoid flicker.
8. **Buttons** are consumed by `handleSleepModes()` (combo + armed countdowns) first; if neither is active, `handleKeyButton` and `handleBootButton` process presses normally.
9. **Sleep**: tier 2 uses `esp_light_sleep_start()` with a 30 s timer and EXT1 wake on both buttons; timer wake does a silent fetch and re-enters. Button wake restarts the device for a known-good state. Tier 3 uses `esp_deep_sleep_start()` with an RTC timer scheduled for the next auto-open time (if enabled) plus EXT1 on both buttons.

### HTTPS

The firmware uses `WiFiClientSecure::setInsecure()`. TLS certificate validation is disabled to avoid bundling and maintaining a root CA store on the device. This is acceptable for a personal device on a trusted network; if you want strict validation, replace with `setCACert()` and ship the relevant root (Cloudflare fronts `claude.ai`, so Baltimore CyberTrust or ISRG Root X1 work).

### Storage

All configuration lives in ESP32 NVS under the `claudestats` namespace. Notable keys:

| Key | Meaning |
|---|---|
| `ssid`, `password` | Last-connected WiFi (cached for Info screen + migration). |
| `wN`, `w0s`..`w7s`, `w0p`..`w7p` | WiFi list count and per-index SSID/password. |
| `sessionKey`, `orgId` | Claude credentials. |
| `aoEn`, `aoHour`, `aoMin`, `aoOff`, `aoLast` | Auto-open enable, UTC hour/minute, local-offset (display), last-fired YYYYMMDD. |
| `idleMin` | Tier 2 timeout in minutes. |
| `adminU`, `adminP` | Web admin credentials. |
| `rotation` | Saved screen orientation. |

Flash wear is negligible — configuration is written once per admin save or per WiFi CRUD operation.

---

## Security notes

- **Basic auth** on the admin page; default `admin / admin`. Change it as soon as the device is on your network. The password is stored in plaintext in NVS (practical for a trusted-network device; treat the board as physically trusted).
- The sessionKey gives full access to your Claude account. Don't flash firmware you haven't audited onto a device holding a production key.
- The daily auto-open endpoint costs one message / day against the weekly quota. Automated traffic patterns can in principle be noticed by the upstream — keep the feature off unless you actually need it.
- TLS cert validation is intentionally disabled. On a trusted LAN this is a fair trade-off for firmware simplicity; replace with `setCACert()` if your threat model needs it.

---

## Troubleshooting

- **Captive portal doesn't open** — browse to `http://192.168.4.1` manually; some browsers cache old 204 responses.
- **`Invalid sessionKey`** at boot — your cookie rotated. Visit the admin UI and paste a fresh value.
- **Stats show `No usage data`** — your plan doesn't expose 5 h / 7 d windows at the moment, or you haven't used Claude recently.
- **Stuck in "Scanning WiFi / Connecting"** — none of your saved WiFis are in range. The device reboots after ~20 s; hold KEY for 10 s during that window and escalate with BOOT 2 s to factory reset.
- **Screen dark on battery** — make sure you're on the current firmware; tier 1 relies on GPIO15 being HIGH to keep the LCD power rail alive when USB is unplugged.
- **Buttons unresponsive after light sleep** — should not happen: tier 2 button wake always reboots the device for a clean state. If you see it, capture a serial log and file an issue.
- **Compile error about TFT_eSPI pins** — `platformio.ini` sets every TFT_eSPI flag as a `-D` build flag. Don't edit `User_Setup.h` inside the library.

---

## Related projects

- **[ClaudeStats](https://github.com/alberthorta/ClaudeStats)** — the macOS menu bar app this project ports to hardware.
- **[she-llac/claude-counter](https://github.com/she-llac/claude-counter)** — credit for discovering the `/api/organizations/{orgId}/usage` endpoint.

---

## License

MIT — see [LICENSE](LICENSE).
