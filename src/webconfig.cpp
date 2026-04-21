#include "webconfig.h"
#include "webui.h"
#include "battery.h"
#include "readme_embed.h"
#include <WebServer.h>
#include <WiFi.h>

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

// HTTP Basic auth gate. Credentials come from cfg.adminUser / adminPassword;
// defaults are admin/admin. Call at the top of every handler. Returns true
// if the request is authorized — the handler should early-return otherwise.
bool requireAuth() {
    if (!server.authenticate(current.adminUser.c_str(),
                              current.adminPassword.c_str())) {
        server.requestAuthentication(BASIC_AUTH, "ClaudeStats",
                                      "Authentication required");
        return false;
    }
    return true;
}

void handleRoot() {
    if (!requireAuth()) return;
    String body; body.reserve(4608);
    body += brandHeader("config");

    // Toolbar with a manual refresh button (page values are snapshotted on
    // render, so batter / status need a reload to update).
    body += F("<div class=toolbar>"
              "<button type=button class=btnGhost onclick=\"location.reload()\" "
              "title=\"Refresh\">&#x21bb; Refresh</button>"
              "</div>");

    // Live battery status block (read when the page is rendered).
    int bmv  = Battery::millivolts();
    int bpct = Battery::percent(bmv);
    bool bchg = Battery::isCharging(bmv);
    body += F("<div class=status>"
              "<div class=statusLabel>Battery</div>"
              "<div class=statusValue>");
    char bb[48];
    if (bchg) {
        snprintf(bb, sizeof(bb), "USB-C charging — %d.%02d V",
                 bmv / 1000, (bmv / 10) % 100);
    } else {
        snprintf(bb, sizeof(bb), "%d%% — %d.%02d V",
                 bpct, bmv / 1000, (bmv / 10) % 100);
    }
    body += bb;
    body += F("</div></div>");

    // Current-connection status
    body += F("<div class=status>"
              "<div class=statusLabel>Connected to</div>"
              "<div class=statusValue>");
    body += current.ssid.length() > 0 ? htmlEscape(current.ssid) : String("—");
    body += F("</div></div>");

    // MAC address
    body += F("<div class=status>"
              "<div class=statusLabel>MAC address</div>"
              "<div class=statusValue>");
    body += WiFi.macAddress();
    body += F("</div></div>");

    // Saved WiFi list (CRUD) — CRUD endpoints don't reboot; only the main
    // save-and-reboot form does.
    body += F("<label style=\"margin-top:18px\">Saved WiFi networks</label>"
              "<div class=wifiList>");
    if (current.wifiCount == 0) {
        body += F("<p class=hint style=\"margin:6px 0 0 0\">"
                  "No networks saved yet. Add one below.</p>");
    } else {
        for (int i = 0; i < current.wifiCount; i++) {
            String idxStr = String(i);
            body += F("<div class=wifiRow><span class=ssid>");
            body += htmlEscape(current.wifis[i].ssid);
            body += F("</span><div class=wifiActions>");

            // Up arrow — disabled on first entry
            body += F("<form method=POST action=/wifi/move>"
                      "<input type=hidden name=idx value=\"");
            body += idxStr;
            body += F("\"><input type=hidden name=delta value=\"-1\">"
                      "<button type=submit class=btnMove aria-label=\"up\"");
            if (i == 0) body += F(" disabled");
            body += F(">&uarr;</button></form>");

            // Down arrow — disabled on last entry
            body += F("<form method=POST action=/wifi/move>"
                      "<input type=hidden name=idx value=\"");
            body += idxStr;
            body += F("\"><input type=hidden name=delta value=\"1\">"
                      "<button type=submit class=btnMove aria-label=\"down\"");
            if (i == current.wifiCount - 1) body += F(" disabled");
            body += F(">&darr;</button></form>");

            // Delete
            body += F("<form method=POST action=/wifi/delete "
                      "onsubmit=\"return confirm('Delete this WiFi?');\">"
                      "<input type=hidden name=idx value=\"");
            body += idxStr;
            body += F("\"><button type=submit>Delete</button></form>");

            body += F("</div></div>");
        }
    }
    body += F("</div>"
              "<form method=POST action=/wifi/add class=addWifi>"
              "<input type=text name=ssid placeholder=SSID required autocomplete=off>"
              "<input type=password name=password placeholder=password autocomplete=off>"
              "<button type=submit>Add</button>"
              "</form>"
              "<p class=hint>At boot the device scans the air and connects to "
              "the strongest match.</p>");

    body += F("<p class=sub style=\"margin-top:20px\">"
              "Other settings. Saving reboots the device.</p>"
              "<div id=saveMsg></div>"
              "<form id=mainForm method=POST action=/save>"
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

              "<label>Idle light sleep <small>(minutes; 0 disables)</small></label>"
              "<input type=number name=idleMin min=0 max=60 step=1 value=\"");
    body += String(current.idleSleepMin);
    body += F("\">"
              "<p class=hint>After this many minutes of backlight-off with no "
              "button press, the device drops into light sleep to save battery. "
              "Any button wakes it back up instantly.</p>"

              "<label style=\"margin-top:18px\">Admin username</label>"
              "<input type=text name=adminUser autocomplete=off value=\"");
    body += htmlEscape(current.adminUser);
    body += F("\">"
              "<label>Admin password <small>(leave blank to keep current)</small></label>"
              "<input type=password name=adminPassword autocomplete=new-password>"
              "<p class=hint>Used to access this admin page. "
              "Factory reset restores the defaults (admin / admin).</p>"

              "<button type=submit>Save &amp; reboot</button>"
              "<p class=hint>Current organization: ");
    body += current.orgId.length() > 0
            ? "<code>" + htmlEscape(current.orgId) + "</code>"
            : "<i>not yet discovered</i>";
    body += F(". It will be rediscovered after saving.</p>"
              "</form>"
              "<div class=footer>"
              "<a href=/readme>Documentation (README)</a>"
              "</div>");

    // Inline JS: converts stored UTC ↔ local using the browser's current
    // offset (so DST is always right as of now). On submit, the hidden
    // aoHour/aoMin are in UTC and aoOff is signed minutes east of UTC. The
    // submit handler posts via fetch() so the page doesn't navigate away on
    // reboot — a success banner stays in place until the user reloads.
    body += "<script>(function(){"
            "var form=document.getElementById('mainForm');"
            "var msg=document.getElementById('saveMsg');"
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
            "function syncTz(){"
              "var v=(document.getElementById('aoTimeLocal').value||'09:00').split(':');"
              "var now=new Date();"
              "var d2=new Date(now.getFullYear(),now.getMonth(),now.getDate(),+v[0],+v[1]);"
              "document.getElementById('aoHour').value=d2.getUTCHours();"
              "document.getElementById('aoMin').value=d2.getUTCMinutes();"
              "document.getElementById('aoOff').value=-now.getTimezoneOffset();"
            "}"
            "function setMsg(kind,text){"
              "msg.innerHTML='<div class=\"alert '+kind+'\">'+text+'</div>';"
            "}"
            "form.addEventListener('submit',function(e){"
              "e.preventDefault();"
              "syncTz();"
              "setMsg('ok','Saving…');"
              "fetch('/save',{method:'POST',body:new FormData(form)})"
              ".then(function(r){"
                "if(r.ok){setMsg('ok','Saved. Device is rebooting — reload in ~10 s.');}"
                "else{r.text().then(function(t){setMsg('err','Save failed: '+t);});}"
              "})"
              ".catch(function(){"
                // Network drop is expected once the device starts rebooting.
                "setMsg('ok','Saved. Device is rebooting — reload in ~10 s.');"
              "});"
            "});"
            "})();</script>";

    server.send(200, "text/html", pageWrap(body));
}

void handleSave() {
    if (!requireAuth()) return;
    String sessionKey = server.arg("sessionKey");
    sessionKey.trim();

    if (sessionKey.isEmpty()) {
        server.send(400, "text/plain", "sessionKey is required");
        return;
    }

    AppConfig updated = current;
    bool sessionChanged = (sessionKey != current.sessionKey);
    updated.sessionKey = sessionKey;
    if (sessionChanged) updated.orgId = "";  // rediscovered after reboot

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

    int idle = server.arg("idleMin").toInt();
    if (idle < 0 || idle > 60) idle = 5;
    updated.idleSleepMin = idle;

    String newUser = server.arg("adminUser");
    newUser.trim();
    if (!newUser.isEmpty()) updated.adminUser = newUser;
    // Empty password field means "keep current"; otherwise replace.
    String newPass = server.arg("adminPassword");
    if (!newPass.isEmpty()) updated.adminPassword = newPass;

    if (!Config::save(updated)) {
        server.send(500, "text/plain", "failed to persist to NVS");
        return;
    }

    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
}

void redirectHome() {
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
}

// Raw README markdown (embedded at build time from README.md). Served for
// the /readme renderer to fetch and parse client-side.
void handleReadmeMd() {
    if (!requireAuth()) return;
    // PROGMEM string; send_P streams from flash without copying into RAM.
    server.send_P(200, "text/markdown; charset=utf-8", README_MD);
}

// HTML shell with an inline markdown → HTML parser that fetches /readme.md
// on load and renders it into the page. No external JS, so this keeps the
// offline-first constraint intact.
void handleReadme() {
    if (!requireAuth()) return;

    String html; html.reserve(8192);
    html += "<!doctype html><html><head>";
    html += WebUi::HEAD_META;
    html += "<title>ClaudeStats — README</title><style>";
    html += WebUi::STYLES;
    html += R"CSS(
      .card { max-width: 820px; }
      #rmContent { font-size: 14px; line-height: 1.6; color: rgba(229,231,235,0.92); }
      #rmContent h1 { font-size: 26px; margin: 24px 0 10px; background: linear-gradient(120deg,#c4b5fd 0%,#f0abfc 50%,#fbbf24 100%); -webkit-background-clip: text; background-clip: text; color: transparent; }
      #rmContent h2 { font-size: 20px; margin: 26px 0 8px; color: #e5e7eb; border-bottom: 1px solid rgba(255,255,255,0.10); padding-bottom: 6px; }
      #rmContent h3 { font-size: 16px; margin: 18px 0 6px; color: #c4b5fd; }
      #rmContent h4 { font-size: 14px; margin: 14px 0 4px; color: rgba(229,231,235,0.9); text-transform: uppercase; letter-spacing: 0.04em; }
      #rmContent p  { margin: 10px 0; }
      #rmContent a  { color: #c4b5fd; text-decoration: none; border-bottom: 1px dotted rgba(196,181,253,0.5); }
      #rmContent a:hover { border-bottom-color: #c4b5fd; }
      #rmContent ul, #rmContent ol { margin: 10px 0 10px 22px; padding: 0; }
      #rmContent li { margin: 4px 0; }
      #rmContent code { font-family: ui-monospace, 'SF Mono', Menlo, monospace; font-size: 12.5px; background: rgba(118,98,255,0.14); color: #f0abfc; padding: 1px 6px; border-radius: 4px; }
      #rmContent pre { background: rgba(11,11,20,0.7); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 12px 14px; overflow-x: auto; margin: 12px 0; }
      #rmContent pre code { background: transparent; color: #e5e7eb; padding: 0; font-size: 12px; }
      #rmContent hr { border: 0; border-top: 1px solid rgba(255,255,255,0.10); margin: 22px 0; }
      #rmContent table { border-collapse: collapse; margin: 12px 0; width: 100%; font-size: 13px; }
      #rmContent th, #rmContent td { border: 1px solid rgba(255,255,255,0.12); padding: 6px 10px; text-align: left; }
      #rmContent th { background: rgba(255,255,255,0.05); font-weight: 600; }
      #rmContent tbody tr:nth-child(odd) { background: rgba(255,255,255,0.02); }
      #rmContent strong { color: #fafafa; }
      #rmContent em { color: rgba(240,171,252,0.95); font-style: normal; }
      .toolbar a.btnGhost { text-decoration: none; }
    )CSS";
    html += "</style></head><body><div class=card>";
    html += "<div class=toolbar>"
            "<a class=btnGhost href=/>&larr; Back</a>"
            "</div>";
    html += "<div id=rmContent>Loading…</div>";
    html += "</div>";

    // Inline markdown parser. Handles: #-###### headings, code fences, inline
    // code, **bold**, *italic*, [text](url), unordered / ordered lists, HR,
    // pipe tables. Good enough for our README. Not a full CommonMark impl.
    html += R"JS(
<script>
function esc(s){return s.replace(/[&<>]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;'}[c];});}
function inl(s){
  return s
    .replace(/`([^`\n]+)`/g,'<code>$1</code>')
    .replace(/\*\*([^*\n]+?)\*\*/g,'<strong>$1</strong>')
    .replace(/(^|[^*])\*(?!\s)([^*\n]+?)\*(?!\*)/g,'$1<em>$2</em>')
    .replace(/\[([^\]]+)\]\(([^)]+)\)/g,'<a href="$2" target="_blank" rel="noopener">$1</a>');
}
function splitRow(l){
  var r=l.trim();
  if(r[0]=='|')r=r.slice(1);
  if(r.slice(-1)=='|')r=r.slice(0,-1);
  return r.split('|').map(function(s){return s.trim();});
}
function md2html(md){
  md=md.replace(/\r\n?/g,'\n');
  var codes=[];
  md=md.replace(/```[a-zA-Z]*\n([\s\S]*?)```/g,function(_,code){
    codes.push(code);return '\u0000CB'+(codes.length-1)+'\u0000';
  });
  md=esc(md);
  var lines=md.split('\n'),out=[],i=0,lt=null;
  function close(){if(lt){out.push('</'+lt+'>');lt=null;}}
  while(i<lines.length){
    var ln=lines[i];
    // Table
    if(ln.indexOf('|')>=0 && i+1<lines.length &&
       /^\s*\|?[-:\s|]+\|?\s*$/.test(lines[i+1]) && lines[i+1].indexOf('-')>=0){
      close();
      var hdr=splitRow(ln);
      i+=2;
      var t='<table><thead><tr>'+hdr.map(function(h){return '<th>'+inl(h)+'</th>';}).join('')+'</tr></thead><tbody>';
      while(i<lines.length && lines[i].indexOf('|')>=0){
        var row=splitRow(lines[i]);
        t+='<tr>'+row.map(function(c){return '<td>'+inl(c)+'</td>';}).join('')+'</tr>';
        i++;
      }
      t+='</tbody></table>';
      out.push(t);
      continue;
    }
    // Heading
    var h=ln.match(/^(#{1,6})\s+(.+)$/);
    if(h){close();out.push('<h'+h[1].length+'>'+inl(h[2])+'</h'+h[1].length+'>');i++;continue;}
    // HR
    if(/^\s*-{3,}\s*$/.test(ln)){close();out.push('<hr>');i++;continue;}
    // Code placeholder on its own line
    if(/^\u0000CB\d+\u0000$/.test(ln)){close();out.push(ln);i++;continue;}
    // Unordered list
    var ul=ln.match(/^[-*+]\s+(.+)$/);
    if(ul){if(lt!=='ul'){close();out.push('<ul>');lt='ul';}out.push('<li>'+inl(ul[1])+'</li>');i++;continue;}
    // Ordered list
    var ol=ln.match(/^\d+\.\s+(.+)$/);
    if(ol){if(lt!=='ol'){close();out.push('<ol>');lt='ol';}out.push('<li>'+inl(ol[1])+'</li>');i++;continue;}
    // Blank
    if(ln.trim()===''){close();i++;continue;}
    // Paragraph
    close();
    var para=[ln];i++;
    while(i<lines.length && lines[i].trim()!=='' &&
          !/^(#{1,6}\s|-{3,}\s*$|[-*+]\s|\d+\.\s)/.test(lines[i]) &&
          !/^\u0000CB\d+\u0000$/.test(lines[i]) &&
          !(lines[i].indexOf('|')>=0 && i+1<lines.length && /^\s*\|?[-:\s|]+\|?\s*$/.test(lines[i+1]) && lines[i+1].indexOf('-')>=0)){
      para.push(lines[i]);i++;
    }
    out.push('<p>'+inl(para.join(' '))+'</p>');
  }
  close();
  var html=out.join('\n');
  html=html.replace(/\u0000CB(\d+)\u0000/g,function(_,idx){
    return '<pre><code>'+esc(codes[+idx])+'</code></pre>';
  });
  return html;
}
fetch('/readme.md')
  .then(function(r){return r.text();})
  .then(function(t){document.getElementById('rmContent').innerHTML=md2html(t);})
  .catch(function(e){document.getElementById('rmContent').textContent='Failed to load README: '+e;});
</script>
)JS";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleWifiAdd() {
    if (!requireAuth()) return;
    String ssid     = server.arg("ssid");
    String password = server.arg("password");
    ssid.trim();
    if (ssid.isEmpty()) { redirectHome(); return; }

    if (!Config::addOrUpdateWifi(current, ssid, password)) {
        String body = brandHeader("config");
        body += F("<div class=\"alert err\">WiFi list is full (max 8).</div>"
                  "<p><a href=/ style=\"color:#c4b5fd\">Back</a></p>");
        server.send(400, "text/html", pageWrap(body));
        return;
    }
    Config::save(current);
    redirectHome();
}

void handleWifiDelete() {
    if (!requireAuth()) return;
    int idx = server.arg("idx").toInt();
    Config::removeWifiAt(current, idx);
    Config::save(current);
    redirectHome();
}

void handleWifiMove() {
    if (!requireAuth()) return;
    int idx   = server.arg("idx").toInt();
    int delta = server.arg("delta").toInt();
    if (Config::moveWifi(current, idx, delta)) {
        Config::save(current);
    }
    redirectHome();
}

} // namespace

void WebConfig::begin(const AppConfig& cfg) {
    current = cfg;
    server.on("/",             HTTP_GET,  handleRoot);
    server.on("/save",         HTTP_POST, handleSave);
    server.on("/wifi/add",     HTTP_POST, handleWifiAdd);
    server.on("/wifi/delete",  HTTP_POST, handleWifiDelete);
    server.on("/wifi/move",    HTTP_POST, handleWifiMove);
    server.on("/readme",       HTTP_GET,  handleReadme);
    server.on("/readme.md",    HTTP_GET,  handleReadmeMd);
    server.onNotFound([]{ server.send(404, "text/plain", "not found"); });
    server.begin();
}

void WebConfig::loop() {
    server.handleClient();
}
