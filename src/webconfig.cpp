#include "webconfig.h"
#include "webui.h"
#include <WebServer.h>

namespace {

WebServer server(80);
AppConfig current;

String htmlEscape(const String& s) {
    String out; out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

String pageWrap(const String& body) {
    String html; html.reserve(6144);
    html += "<!doctype html><html><head>";
    html += WebUi::HEAD_META;
    html += "<title>ClaudeStats config</title><style>";
    html += WebUi::STYLES;
    html += "</style></head><body><div class=card>";
    html += body;
    html += "</div></body></html>";
    return html;
}

String brandHeader(const char* badge = nullptr) {
    String s = "<div class=brand>";
    s += WebUi::ICON_SVG;
    s += "<h1>ClaudeStats</h1>";
    if (badge) { s += "<span class=badge>"; s += badge; s += "</span>"; }
    s += "</div>";
    return s;
}

void handleRoot() {
    String body; body.reserve(4096);
    body += brandHeader("config");
    body += F("<p class=sub>Edit the device configuration. "
              "Saving reboots the device.</p>"
              "<form method=POST action=/save>"
              "<label>WiFi SSID</label>"
              "<input type=text name=ssid required autocomplete=off value=\"");
    body += htmlEscape(current.ssid);
    body += F("\">"
              "<label>WiFi password <small>(leave blank to keep current)</small></label>"
              "<input type=password name=password autocomplete=off>"
              "<label>Claude sessionKey</label>"
              "<textarea name=sessionKey required autocomplete=off>");
    body += htmlEscape(current.sessionKey);
    body += F("</textarea>"

              "<label style=\"display:flex;align-items:center;gap:8px;margin-top:20px;\">"
              "<input type=checkbox name=aoEn id=aoEn style=\"width:auto;margin:0;\">"
              "<span>Auto-open 5h window daily</span>"
              "</label>"
              "<label>Trigger time <small>(your local time)</small></label>"
              "<input type=time id=aoTimeLocal value=\"09:00\" step=60>"
              "<input type=hidden name=aoHour id=aoHour>"
              "<input type=hidden name=aoMin  id=aoMin>"
              "<input type=hidden name=aoOff  id=aoOff>"
              "<p class=hint>At this time each day the device sends one "
              "minimal message to claude.ai so a fresh 5h window starts "
              "exactly when you want it. Off by default.</p>"

              "<button type=submit>Save &amp; reboot</button>"
              "<p class=hint>Current organization: ");
    body += current.orgId.length() > 0
            ? "<code>" + htmlEscape(current.orgId) + "</code>"
            : "<i>not yet discovered</i>";
    body += F(". It will be rediscovered after saving.</p>"
              "</form>");

    // Inline JS: converts stored UTC ↔ local using the browser's current
    // offset (so DST is always right as of now). On submit, the hidden
    // aoHour/aoMin are in UTC and aoOff is signed minutes east of UTC.
    body += "<script>(function(){"
            "var enabled=";
    body += current.autoOpenEnabled ? "true" : "false";
    body += ";var uh=";
    body += String(current.autoOpenHourUtc);
    body += ";var um=";
    body += String(current.autoOpenMinuteUtc);
    body += ";document.getElementById('aoEn').checked=enabled;"
            "var d=new Date();d.setUTCHours(uh,um,0,0);"
            "var lh=('0'+d.getHours()).slice(-2),lm=('0'+d.getMinutes()).slice(-2);"
            "document.getElementById('aoTimeLocal').value=lh+':'+lm;"
            "document.querySelector('form').addEventListener('submit',function(){"
            "var v=(document.getElementById('aoTimeLocal').value||'09:00').split(':');"
            "var now=new Date();"
            "var d2=new Date(now.getFullYear(),now.getMonth(),now.getDate(),+v[0],+v[1]);"
            "document.getElementById('aoHour').value=d2.getUTCHours();"
            "document.getElementById('aoMin').value=d2.getUTCMinutes();"
            "document.getElementById('aoOff').value=-now.getTimezoneOffset();"
            "});})();</script>";

    server.send(200, "text/html", pageWrap(body));
}

void handleSave() {
    String ssid       = server.arg("ssid");
    String password   = server.arg("password");
    String sessionKey = server.arg("sessionKey");
    sessionKey.trim();

    if (ssid.isEmpty() || sessionKey.isEmpty()) {
        String body = brandHeader("config");
        body += F("<div class=\"alert err\">Missing fields.</div>"
                  "<p><a href=/ style=\"color:#c4b5fd\">Back</a></p>");
        server.send(400, "text/html", pageWrap(body));
        return;
    }

    AppConfig updated = current;
    updated.ssid       = ssid;
    if (password.length() > 0) updated.password = password;
    updated.sessionKey = sessionKey;
    updated.orgId      = "";  // rediscovered after reboot

    updated.autoOpenEnabled   = server.hasArg("aoEn");
    int h = server.arg("aoHour").toInt();
    int m = server.arg("aoMin").toInt();
    int o = server.arg("aoOff").toInt();
    if (h < 0 || h > 23) h = 9;
    if (m < 0 || m > 59) m = 0;
    if (o < -720 || o > 840) o = 0;
    updated.autoOpenHourUtc   = h;
    updated.autoOpenMinuteUtc = m;
    updated.autoOpenOffsetMin = o;

    if (!Config::save(updated)) {
        String body = brandHeader("config");
        body += F("<div class=\"alert err\">Failed to save.</div>"
                  "<p><a href=/ style=\"color:#c4b5fd\">Back</a></p>");
        server.send(500, "text/html", pageWrap(body));
        return;
    }

    String body = brandHeader();
    body += F("<div class=\"alert ok\">Saved. The device is rebooting…</div>");
    server.send(200, "text/html", pageWrap(body));
    delay(500);
    ESP.restart();
}

} // namespace

void WebConfig::begin(const AppConfig& cfg) {
    current = cfg;
    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound([]{ server.send(404, "text/plain", "not found"); });
    server.begin();
}

void WebConfig::loop() {
    server.handleClient();
}
