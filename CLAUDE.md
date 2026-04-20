# CLAUDE.md

Notes for future Claude sessions working on this repo.

## What this is

ESP32 firmware for a **LilyGo T-Display S3** that shows Claude Code subscription usage on a 1.9" display. It is the portable / hardware counterpart of the macOS app at `../ClaudeStats`. The device is fully autonomous — it talks directly to `claude.ai/api` over HTTPS. There is no companion app or bridge.

Albert owns both projects.

## Board

- LilyGo T-Display S3, ESP32-S3, 16 MB flash, 8 MB PSRAM
- Display: ST7789 170×320 parallel 8-bit; TFT_eSPI pin mapping is in `platformio.ini` build flags
- KEY button on GPIO14 (active low, internal pull-up); BOOT on GPIO0
- **GPIO15 = LCD_POWER_ON**: must be driven HIGH at boot or the panel stays dark on battery (USB feeds the rail directly). Handled in `Display::begin()`. Do not remove.
- **Battery**: LiPo (2×500 mAh in parallel in the current build) via the board's JST connector; TP4054 onboard charger from USB (~500 mA). Voltage read on **GPIO4** through a 2:1 resistor divider — use `analogReadMilliVolts(4) * 2`. See `battery.cpp`.

## Build / flash / logs

```bash
pio run                                     # compile
pio run -t upload                           # compile + flash (autodetects /dev/cu.usbmodem101)
pio device monitor -b 115200                # serial logs (115200 baud, USB CDC)
```

First build pulls ESP32 platform + TFT_eSPI + ArduinoJson (~2–3 min). Incremental builds are ~20 s. Flash takes ~10 s at 921600 baud.

## File layout

```
src/
├── main.cpp          setup/loop, button debounce, 30 s fetch timer, view state, scheduler
├── config.{h,cpp}    Preferences wrapper (NVS ns = "claudestats")
├── display.{h,cpp}   TFT_eSPI + off-screen TFT_eSprite (PSRAM) for flicker-free redraws
├── battery.{h,cpp}   ADC read + LiPo percent curve + charging heuristic
├── provisioning.{h,cpp}  SoftAP + DNSServer + captive portal form (AP-only mode)
├── webconfig.{h,cpp}     WebServer on STA IP for re-configuration
├── api.{h,cpp}       WiFiClientSecure + HTTPClient + ArduinoJson
├── stats.{h,cpp}     Pace struct + thresholds (matches Swift app exactly)
└── webui.{h,cpp}     Shared inline CSS + SVG logo used by both web pages
```

## Design decisions to respect

- **Offline-first web UI**: the provisioning captive portal runs with no internet. Do NOT introduce Tailwind CDN / Google Fonts / external scripts in `webui.cpp`. The shared CSS is hand-rolled to mimic the ClaudeStats website aesthetic (mesh gradient, glass card, brand gradient title). The SVG logo is inline.
- **Reset preserves sessionKey**: long-press KEY calls `Config::clearWifi()` (removes `ssid`+`password` only). `sessionKey` and `orgId` survive so the user can change WiFi without re-entering Claude credentials. Don't collapse this back into a full `Config::clear()`.
- **orgId is auto-discovered** from the sessionKey via `GET /api/organizations`. Forms never ask for it. On save, the stored `orgId` is wiped so the next boot rediscovers — this handles sessionKey → different account cleanly.
- **HTTPS**: `WiFiClientSecure::setInsecure()`. We intentionally skip TLS cert validation to avoid maintaining a root CA bundle on the device. Discussed and accepted.
- **Sprite-based rendering**: `showStats` / `showInfo` / `showLoading` / `showApiError` draw into an off-screen 320×170×16 sprite allocated in PSRAM, pushed with `pushSprite`. Simple screens (`showProvisioning`, `showConnecting`, `showResetting`) fall back to direct TFT drawing (sprite is torn down and recreated). Keep this split.
- **Pace thresholds must match the Swift app**: 0.75 / 0.95 / 1.10 / 1.35 (`Well under pace` / `Under pace` / `On pace` / `Over pace` / `Burning fast`). Colors: green / mint / yellow / orange / red. Defined in `stats.cpp`.
- **Adaptive layout**: when only one of the two windows has data, the panel expands to full screen (taller bar). Handled in `display.cpp::showStats`.
- **NTP**: time is set to UTC via `setenv("TZ","UTC0",1)` + `tzset()` + `configTime(0,0,…)`. `resets_at` ISO-8601 strings are parsed with a manual sscanf + `mktime`, which returns UTC epoch because of the TZ setting. Don't switch to localtime-based parsing.
- **Battery indicator** (`showStats` top-right, Info screen line): fill color is green/yellow/red by percent; border and label turn cyan (`COLOR_ACCENT`) when `Battery::isCharging()` is true (voltage ≥ 4150 mV), and the text switches to `USBC` in that case. The percent curve is piecewise-linear for LiPo — keep it in sync with `battery.cpp` if you adjust it.
- **Landscape vs portrait header**: the "refresh Ns" countdown is only drawn in landscape (the 170 px portrait header has no room left of the battery icon). CACHED badge slots to the left of whatever comes next.
- **Screen sleep**: holding BOOT for 2 s toggles the backlight off via `Display::setBacklight(false)`. Panel power (GPIO15) and the sprite stay alive so waking (any button press) is instant and repaints from the cached sprite plus a fresh `redraw()`. A wake-up press is "consumed" (doesn't also rotate / toggle view) via the `*WakeOnly` flags in `main.cpp`.
- **Daily 5h window auto-open** (opt-in, off by default): the device fires one minimal `POST` to `claude.ai/api/organizations/{org}/chat_conversations` + completion + DELETE each day at a user-chosen time, so a fresh 5 h rate-limit window starts at a convenient moment for heavy-burst users. Scheduling:
  - Trigger time is stored in UTC (`autoOpenHourUtc`, `autoOpenMinuteUtc`); the web form edits in the browser's local time and converts via `getTimezoneOffset()`. The stored `autoOpenOffsetMin` is only used to display the schedule back in local time on the Info screen — the scheduler itself compares UTC.
  - Anti-double-fire: `lastAutoOpenDate` (YYYYMMDD UTC) persisted in NVS key `aoLast`. One attempt per day, success or failure, so a transient error doesn't spam claude.ai.
  - The exact request body of the `completion` POST is still being iterated — `Api::openWindow` logs everything verbosely over Serial for this. Once the shape is confirmed, keep the verbose logs behind a flag rather than deleting them outright.

## Data source

```
GET https://claude.ai/api/organizations                    -> [{uuid, name?}]
GET https://claude.ai/api/organizations/{uuid}/usage       -> {five_hour?, seven_day?}
```

Both require `Cookie: sessionKey=…`. Response fields used:

- `five_hour.utilization` — 0..100, may be null / window missing
- `five_hour.resets_at` — ISO-8601 UTC
- `seven_day.utilization` / `resets_at` — same

The 5-hour window is not always present (users who haven't started a session). Handle `!valid`.

## Testing changes

- **Provisioning page** without a Mac in the loop: long-press KEY → device goes to AP mode → connect phone to `ClaudeStats` WiFi → captive portal should auto-pop → form prefills existing sessionKey.
- **Edit page**: short-press KEY → read IP off the Info screen → open in any browser on the same network.
- **Reset logic**: after long-press + reboot, confirm the Info screen still shows the original sessionKey. `Config::clear()` still exists but is unused in normal flow — only call it if you need a total wipe (sessionKey expired beyond recovery, device being handed off).
- **Auto-open**: enable in the web form with a trigger time 1–2 min in the future, save, watch `pio device monitor` at the trigger moment. The Serial log dumps each step of `openWindow` so the endpoint shape can be iterated without re-flashing between attempts (edit payload → flash → re-trigger via checking the box with a near-future time again).
- **Screen sleep**: hold BOOT 2 s to sleep; any button press wakes. Verify the wake press doesn't also trigger rotate/toggle-view.
- **Battery**: on USB the header shows `USBC` (cyan). Unplug and the indicator should drop to an actual % with color changing from green → yellow → red as the pack discharges.

## What not to do

- Don't introduce a companion Mac app / bridge. The user explicitly chose the autonomous option over a bridge at the start.
- Don't add JSONL / local file parsing. Only the remote `usage` endpoint is reachable from the device.
- Don't parse `localtime` for `resets_at`. Keep UTC.
- Don't add `orgId` back as a form field. It's auto-discovered.
- Don't bundle Tailwind or any web CDN for the captive portal.

## Git / publish

Repo: `github.com/alberthorta/ClaudeStatsPortable` (public, MIT). `.gitignore` excludes `.pio/`, `.vscode/`, `.DS_Store`.
