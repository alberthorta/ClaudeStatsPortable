#include "provisioning.h"
#include "display.h"
#include "webui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>

namespace {

const char*     AP_SSID = "ClaudeStats";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

DNSServer  dns;
WebServer  server(80);
AppConfig  preset;
AppConfig  result;
bool       done = false;
String     scanOptionsHtml;

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

void rescanNetworks() {
    int n = WiFi.scanNetworks();
    scanOptionsHtml = "";
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        String sel = (preset.ssid.length() > 0 && ssid == preset.ssid) ? " selected" : "";
        scanOptionsHtml += "<option value=\"" + htmlEscape(ssid) + "\"" + sel + ">"
                        +  htmlEscape(ssid) + " (" + String(WiFi.RSSI(i)) + " dBm)"
                        +  "</option>";
    }
    WiFi.scanDelete();
}

String pageWrap(const String& body) {
    String html; html.reserve(6144);
    html += "<!doctype html><html><head>";
    html += WebUi::HEAD_META;
    html += "<title>ClaudeStats setup</title><style>";
    html += WebUi::STYLES;
    html += "</style></head><body><div class=card>";
    html += body;
    html += "</div></body></html>";
    return html;
}

String formBody(const String& errorMsg) {
    String body; body.reserve(3072);
    body += "<div class=brand>";
    body += WebUi::ICON_SVG;
    body += "<h1>ClaudeStats</h1><span class=badge>setup</span></div>";
    body += F("<p class=sub>Connect the device to your WiFi and sign it in to Claude. "
              "The organization is discovered automatically.</p>");
    if (errorMsg.length() > 0) {
        body += "<div class=\"alert err\">" + htmlEscape(errorMsg) + "</div>";
    }
    body += F("<form method=POST action=/save>"
              "<label>WiFi network</label>"
              "<select name=ssid required>");
    body += scanOptionsHtml;
    body += F("</select>"
              "<label>WiFi password</label>"
              "<input type=password name=password autocomplete=off>"
              "<label>Claude sessionKey</label>"
              "<textarea name=sessionKey required autocomplete=off "
              "placeholder=\"sk-ant-sid01-...\">");
    body += htmlEscape(preset.sessionKey);
    body += F("</textarea>"
              "<button type=submit>Save &amp; connect</button>"
              "<p class=hint>The device will try to connect and reach the internet "
              "before saving. If anything fails, you'll come back here.</p>"
              "</form>");
    return body;
}

void handleRoot() {
    server.send(200, "text/html", pageWrap(formBody("")));
}

bool tryConnectSta(const String& ssid, const String& password) {
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

bool hasInternet() {
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    if (!http.begin("http://clients3.google.com/generate_204")) return false;
    int code = http.GET();
    http.end();
    return code == 204 || code == 200;
}

void handleSave() {
    String ssid       = server.arg("ssid");
    String password   = server.arg("password");
    String sessionKey = server.arg("sessionKey");
    sessionKey.trim();

    if (ssid.isEmpty() || sessionKey.isEmpty()) {
        server.send(400, "text/html", pageWrap(formBody("Missing required fields.")));
        return;
    }

    if (!tryConnectSta(ssid, password)) {
        WiFi.disconnect(false, true);
        server.send(200, "text/html",
                    pageWrap(formBody("Could not connect to \"" + ssid + "\". Check password.")));
        return;
    }

    if (!hasInternet()) {
        WiFi.disconnect(false, true);
        server.send(200, "text/html",
                    pageWrap(formBody("Connected to WiFi but no internet access.")));
        return;
    }

    AppConfig cfg;
    cfg.ssid       = ssid;
    cfg.password   = password;
    cfg.sessionKey = sessionKey;
    cfg.orgId      = (sessionKey == preset.sessionKey) ? preset.orgId : "";

    if (!Config::save(cfg)) {
        server.send(500, "text/html", pageWrap(formBody("Failed to save config to NVS.")));
        return;
    }

    String okBody;
    okBody += "<div class=brand>";
    okBody += WebUi::ICON_SVG;
    okBody += "<h1>ClaudeStats</h1></div>";
    okBody += F("<div class=\"alert ok\">Saved. The device is rebooting…</div>");
    server.send(200, "text/html", pageWrap(okBody));
    result = cfg;
    done   = true;
}

void handleCaptivePortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

} // namespace

AppConfig Provisioning::run(const AppConfig& presetIn) {
    preset = presetIn;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
    WiFi.softAP(AP_SSID);
    delay(200);

    rescanNetworks();

    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);

    server.on("/generate_204",              handleCaptivePortal);
    server.on("/gen_204",                   handleCaptivePortal);
    server.on("/hotspot-detect.html",       handleCaptivePortal);
    server.on("/library/test/success.html", handleCaptivePortal);
    server.on("/connecttest.txt",           handleCaptivePortal);
    server.on("/ncsi.txt",                  handleCaptivePortal);
    server.on("/redirect",                  handleCaptivePortal);

    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleCaptivePortal);
    server.begin();

    Display::showProvisioning(AP_SSID, AP_IP.toString());

    done = false;
    while (!done) {
        dns.processNextRequest();
        server.handleClient();
        delay(2);
    }

    server.stop();
    dns.stop();
    delay(500);
    return result;
}
