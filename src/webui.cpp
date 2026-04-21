#include "webui.h"

namespace WebUi {

const char* const HEAD_META =
    "<meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">";

const char* const STYLES = R"CSS(
*, *::before, *::after { box-sizing: border-box; }
html, body { margin:0; padding:0; }
body {
  font-family: ui-rounded,-apple-system,system-ui,'SF Pro Rounded',sans-serif;
  min-height: 100vh;
  color: rgba(229,231,235,0.92);
  background:
    radial-gradient(at 15% 20%, rgba(118,98,255,0.35) 0, transparent 50%),
    radial-gradient(at 85% 10%, rgba(240,171,252,0.22) 0, transparent 50%),
    radial-gradient(at 50% 95%, rgba(251,191,36,0.18) 0, transparent 50%),
    #0b0b14;
  display:flex; align-items:flex-start; justify-content:center;
  padding: 32px 16px 48px;
  -webkit-font-smoothing: antialiased;
}
.card {
  width:100%; max-width:440px;
  background: rgba(255,255,255,0.05);
  border: 1px solid rgba(255,255,255,0.10);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border-radius: 24px;
  padding: 28px 24px;
  box-shadow: 0 30px 60px -20px rgba(0,0,0,0.6);
}
.brand { display:flex; align-items:center; gap:12px; margin-bottom: 6px; }
.brand .icon { width:44px; height:44px; display:block;
  filter: drop-shadow(0 0 18px rgba(118,98,255,0.45)); }
.brand h1 {
  font-size: 22px; font-weight: 700; margin: 0; letter-spacing: -0.01em;
  background: linear-gradient(120deg,#c4b5fd 0%,#f0abfc 50%,#fbbf24 100%);
  -webkit-background-clip: text; background-clip: text; color: transparent;
}
.badge { display:inline-block; padding:2px 10px; border-radius:999px;
  background: rgba(118,98,255,0.18); color:#c4b5fd;
  font-size:11px; font-weight:500; margin-left: 4px; }
.sub { color: rgba(163,163,163,0.88); font-size: 13px;
  margin: 4px 0 16px 0; line-height: 1.45; }
label { display:block; font-size:13px; font-weight:500;
  color: rgba(229,231,235,0.88); margin: 14px 0 6px 0; }
label small { color: rgba(156,163,175,0.8); font-weight:400; font-size:12px; }
input, select, textarea {
  display:block; width:100%;
  padding: 11px 12px;
  background: rgba(11,11,20,0.65);
  color: #fafafa;
  border: 1px solid rgba(255,255,255,0.10);
  border-radius: 12px;
  font-size: 14px; font-family: inherit; outline: none;
  transition: border-color 0.15s, box-shadow 0.15s;
}
textarea {
  font-family: ui-monospace,'SF Mono',Menlo,monospace;
  font-size: 12px; min-height: 96px; resize: vertical;
}
input:focus, select:focus, textarea:focus {
  border-color: rgba(118,98,255,0.7);
  box-shadow: 0 0 0 3px rgba(118,98,255,0.22);
}
button {
  display: inline-flex; align-items: center; justify-content: center;
  margin-top: 20px; width: 100%;
  padding: 12px 20px;
  background: white;
  color: #0b0b14;
  font-family: inherit; font-weight: 600; font-size: 15px;
  border: 0; border-radius: 999px;
  cursor: pointer;
  box-shadow: 0 0 48px rgba(118,98,255,0.32);
  transition: transform 0.12s ease, box-shadow 0.2s;
}
button:hover { transform: scale(1.02); box-shadow: 0 0 64px rgba(118,98,255,0.5); }
.alert { padding: 11px 14px; border-radius: 12px;
  font-size: 13px; margin-bottom: 14px; }
.alert.err { background: rgba(239,68,68,0.12); color: #fca5a5;
  border: 1px solid rgba(239,68,68,0.30); }
.alert.ok  { background: rgba(34,197,94,0.12); color: #86efac;
  border: 1px solid rgba(34,197,94,0.30); }
.hint { color: rgba(156,163,175,0.8); font-size: 12px;
  margin: 10px 0 0 0; line-height: 1.45; }
.footer { margin-top: 22px; padding-top: 14px; border-top: 1px solid rgba(255,255,255,0.08);
  text-align: center; font-size: 12px; }
.footer a { color: rgba(196,181,253,0.85); text-decoration: none; }
.footer a:hover { color: #c4b5fd; text-decoration: underline; }
.toolbar { display:flex; justify-content:flex-end; margin: 4px 0 10px 0; }
.btnGhost {
  display:inline-flex; align-items:center; gap:6px;
  margin:0; padding: 6px 12px; width:auto;
  background: rgba(255,255,255,0.06); color: rgba(229,231,235,0.88);
  border: 1px solid rgba(255,255,255,0.12);
  border-radius: 999px; font-size: 12px; font-family: inherit;
  cursor: pointer; box-shadow: none; }
.btnGhost:hover {
  transform: none; background: rgba(255,255,255,0.12); box-shadow: none; }
.status { display:flex; justify-content:space-between; align-items:center;
  gap:10px; padding:10px 14px; margin: 8px 0 4px 0;
  background: rgba(255,255,255,0.04);
  border: 1px solid rgba(255,255,255,0.08);
  border-radius: 12px; font-size: 13px; }
.statusLabel { color: rgba(156,163,175,0.9); text-transform: uppercase;
  letter-spacing: 0.06em; font-size: 11px; }
.statusValue { color: rgba(229,231,235,0.95); font-weight: 500;
  font-variant-numeric: tabular-nums; }
.wifiList { margin: 10px 0 0 0; }
.wifiRow { display:flex; justify-content:space-between; align-items:center;
  padding: 8px 12px; margin-bottom: 6px;
  background: rgba(255,255,255,0.03);
  border: 1px solid rgba(255,255,255,0.08);
  border-radius: 10px; font-size: 13px; }
.wifiRow .ssid { font-weight: 500; word-break: break-all; margin-right: 10px;
  flex: 1; min-width: 0; }
.wifiRow form { margin:0; }
.wifiActions { display:flex; align-items:center; gap: 4px; flex-shrink: 0; }
.wifiRow button {
  margin: 0; padding: 4px 10px; width:auto; font-size: 12px;
  background: rgba(239,68,68,0.12); color: #fca5a5;
  border: 1px solid rgba(239,68,68,0.30); box-shadow: none; }
.wifiRow button:hover { transform: none;
  background: rgba(239,68,68,0.20); box-shadow: none; }
.wifiRow button.btnMove {
  background: rgba(255,255,255,0.06); color: rgba(229,231,235,0.85);
  border: 1px solid rgba(255,255,255,0.12);
  padding: 4px 8px; font-size: 13px; line-height: 1; }
.wifiRow button.btnMove:hover {
  background: rgba(255,255,255,0.12); }
.wifiRow button.btnMove[disabled] {
  opacity: 0.35; cursor: not-allowed;
  background: rgba(255,255,255,0.03); }
.wifiRow button.btnMove[disabled]:hover { background: rgba(255,255,255,0.03); }
.addWifi { display:grid; grid-template-columns: 1fr 1fr auto; gap: 6px;
  margin-top: 6px; }
.addWifi input { padding: 8px 10px; font-size: 13px; }
.addWifi button { margin: 0; width: auto; padding: 8px 14px; font-size: 13px;
  background: rgba(118,98,255,0.20); color: #c4b5fd;
  border: 1px solid rgba(118,98,255,0.35); box-shadow: none; }
.addWifi button:hover { transform: none;
  background: rgba(118,98,255,0.30); box-shadow: none; }
)CSS";

const char* const ICON_SVG = R"SVG(
<svg class="icon" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
  <defs>
    <linearGradient id="g" x1="0.1" y1="1" x2="0.9" y2="1">
      <stop offset="0" stop-color="#34d399"/>
      <stop offset="0.45" stop-color="#facc15"/>
      <stop offset="0.80" stop-color="#fb923c"/>
      <stop offset="1" stop-color="#ef4444"/>
    </linearGradient>
  </defs>
  <rect x="2" y="2" width="96" height="96" rx="22" ry="22" fill="#1a1f2e"/>
  <path d="M 22.3 72 A 32 32 0 1 1 77.7 72" stroke="url(#g)" stroke-width="8" stroke-linecap="round" fill="none"/>
  <path d="M 34.4 65 A 18 18 0 1 1 65.6 65" stroke="#ffffff" stroke-width="6" stroke-linecap="round" fill="none"/>
  <circle cx="65.6" cy="65" r="4" fill="#fb923c"/>
</svg>
)SVG";

} // namespace WebUi
