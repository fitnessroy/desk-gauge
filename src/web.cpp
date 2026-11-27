#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "claude_auth.h"
#include "claude_source.h"
#include "config.h"
#include "device.h"
#include "model.h"
#include "settings.h"
#include "ui.h"

namespace web {

static WebServer server(80);
static RenderFn renderFace;
static ActionFn replaySplash, playSaver;
static int faces = 1;
static uint32_t restartAt = 0;  // set by Restart / Save & restart, once the answer is sent

static const char kIndex[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Desk Gauge</title>
<style>
:root{color-scheme:dark}
body{background:#111;color:#ccc;font:15px system-ui,sans-serif;display:flex;flex-direction:column;
align-items:center;gap:20px;padding:24px 16px;margin:0}
.row{display:flex;gap:32px;flex-wrap:wrap;justify-content:center}
img{width:240px;height:240px;border-radius:50%;box-shadow:0 0 0 10px #222,0 0 30px #000}
section{background:#1a1a1a;border:1px solid #2a2a2a;border-radius:12px;padding:16px 18px;width:100%;max-width:440px;box-sizing:border-box}
h2{margin:0 0 10px;font-size:15px;color:#d97757}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:8px 0}
input,select,button{font:inherit;background:#222;color:#eee;border:1px solid #333;border-radius:8px;padding:6px 10px}
button{cursor:pointer;background:#d97757;border-color:#d97757;color:#111;font-weight:600}
button.ghost{background:#222;color:#ccc;border-color:#333}
input[type=text]{width:100%;box-sizing:border-box}
#device input:not([type=checkbox]),#device select{width:200px;box-sizing:border-box}
details summary{cursor:pointer;color:#aaa;margin:10px 0}
button.danger{background:#222;color:#eb5046;border-color:#5a2a26}
.muted{color:#888;font-size:13px} .err{color:#eb5046} .ok{color:#6ec878}
a{color:#d97757}
</style></head><body>
<div class="row" id="faces"></div>

<section id="welcome" hidden>
<h2>Set up your Desk Gauge</h2>
<p class="muted">Pick your Wi-Fi network (2.4 GHz) and choose a password for the gauge's web page.
After the restart, connect back to your own Wi-Fi and open <b>http://desk-gauge.local/</b>; the address is
also on the gauge's screen. Log in as <b>admin</b> with the password you choose here, then sign in to Claude.</p>
</section>

<section id="claude">
<h2>Claude</h2>
<div id="status" class="muted">...</div>
<div id="signedout" hidden>
  <p class="muted">The gauge signs in to your Claude account by itself. This is separate from Claude Code
  on your computer, so it never logs that out.</p>
  <p><button onclick="startLogin()">1. Open Claude sign-in</button></p>
  <p class="muted">2. Approve, then copy the code Claude shows you and paste it here:</p>
  <p><input type="text" id="code" placeholder="paste code#state"></p>
  <p><button onclick="finishLogin()">3. Connect</button> <span id="loginmsg"></span></p>
</div>
<div id="signedin" hidden>
  <p><button class="ghost" onclick="post('/poll').then(load)">Refresh now</button>
  <button class="ghost" onclick="if(confirm('Sign the gauge out?'))post('/logout').then(load)">Sign out</button></p>
</div>
</section>

<section id="settings">
<h2>Settings</h2>
<label>Poll every <span><input type="number" id="poll" min="120" max="3600" style="width:80px"> s</span></label>
<label>Face <select id="face"><option value="0">Rings</option><option value="1">Bars</option><option value="2">Rotate</option></select></label>
<label>Theme <select id="theme"></select></label>
<label>Orientation <select id="rot"><option value="0">0°</option><option value="1">90°</option><option value="2">180°</option><option value="3">270°</option></select></label>
<label>Brightness <input type="range" id="bright" min="10" max="255"></label>
<label>Night mode <input type="checkbox" id="non"></label>
<label>Night from <span><input type="time" id="nstart"> to <input type="time" id="nend"></span></label>
<label>Night brightness <span><input type="range" id="nbright" min="0" max="255"> <small class="muted">left = screen off</small></span></label>
<p><button onclick="save()">Save</button> <span id="savemsg" class="muted"></span></p>
<label>Screensaver every <span><input type="number" id="saver" min="0" max="1440" style="width:80px"> min <small class="muted">0 = off</small></span></label>
<p><button class="ghost" onclick="post('/splash')">Replay intro</button>
<button class="ghost" onclick="post('/saver')">Play screensaver</button></p>
</section>

<section id="device">
<h2>Device</h2>
<label id="netrow" hidden>Networks found <select id="nets" onchange="if(this.value)$('ssid').value=this.value"></select></label>
<label>Wi-Fi network <input id="ssid" autocomplete="off"></label>
<label>Wi-Fi password <input type="password" id="pass" autocomplete="new-password"></label>
<p class="muted" id="scanmsg"><button class="ghost" type="button" onclick="scan()">Find networks</button></p>
<label>Page password <input type="password" id="webpass" autocomplete="new-password"></label>
<label>Time zone <select id="tz"></select></label>
<label id="tzcustom" hidden>POSIX TZ <input id="tzposix" placeholder="CET-1CEST,M3.5.0,M10.5.0/3"></label>
<details><summary>Optional: backup network, printer, token bridge</summary>
<label>Backup network <input id="ssid2" autocomplete="off"></label>
<label>Backup password <input type="password" id="pass2" autocomplete="new-password"></label>
<label>Printer IP <input id="bip" placeholder="192.168.1.50"></label>
<label>Printer serial <input id="bserial"></label>
<label>Access code <input type="password" id="bcode" autocomplete="new-password"></label>
<label>Bridge URL <input id="bridge" placeholder="http://192.168.1.10:8787/claude.json"></label>
</details>
<p><button onclick="saveDevice()">Save &amp; restart</button> <span id="devmsg"></span></p>
<p id="devbuttons"><button class="ghost" onclick="restart()">Restart</button>
<button class="danger" onclick="factoryReset()">Factory reset</button></p>
<p class="muted">Lost the page password, or moved the gauge to a new network? Unplug the gauge while the
intro plays, twice in a row, then plug it in: its setup hotspot opens, with the details on the screen.</p>
</section>

<small><a href="/state.json">state.json</a> · firmware <span id="ver"></span></small>
<script>
const $=id=>document.getElementById(id);
const post=(u,body)=>fetch(u,{method:'POST',headers:{'X-Gauge':'1'},body:body?new URLSearchParams(body):undefined});
let FACES=1,HOTSPOT=false;
// Common time zones as POSIX TZ strings, with the browser zones (IANA) that pick them by default.
const TZ=[
['UTC','UTC0','UTC Etc/'],
['UK, Ireland','GMT0BST,M3.5.0/1,M10.5.0','Europe/London Europe/Dublin'],
['Portugal','WET0WEST,M3.5.0/1,M10.5.0','Europe/Lisbon Atlantic/Madeira'],
['Central Europe (Oslo, Berlin, Paris)','CET-1CEST,M3.5.0,M10.5.0/3','Europe/'],
['Eastern Europe (Helsinki, Athens, Kyiv)','EET-2EEST,M3.5.0/3,M10.5.0/4','Europe/Helsinki Europe/Athens Europe/Kyiv Europe/Kiev Europe/Riga Europe/Tallinn Europe/Vilnius Europe/Sofia Europe/Bucharest'],
['Turkey','<+03>-3','Europe/Istanbul'],
['Moscow','MSK-3','Europe/Moscow'],
['Gulf (Dubai)','<+04>-4','Asia/Dubai Asia/Muscat'],
['India','IST-5:30','Asia/Kolkata Asia/Calcutta'],
['Bangkok, Jakarta','<+07>-7','Asia/Bangkok Asia/Jakarta Asia/Ho_Chi_Minh'],
['China, Singapore, Perth','CST-8','Asia/Shanghai Asia/Singapore Asia/Hong_Kong Asia/Taipei Australia/Perth Asia/Kuala_Lumpur Asia/Manila'],
['Japan, Korea','JST-9','Asia/Tokyo Asia/Seoul'],
['Brisbane','AEST-10','Australia/Brisbane'],
['Sydney, Melbourne','AEST-10AEDT,M10.1.0,M4.1.0/3','Australia/'],
['Adelaide','ACST-9:30ACDT,M10.1.0,M4.1.0/3','Australia/Adelaide'],
['New Zealand','NZST-12NZDT,M9.5.0,M4.1.0/3','Pacific/Auckland'],
['US Eastern','EST5EDT,M3.2.0,M11.1.0','America/New_York America/Detroit America/Toronto America/Indiana'],
['US Central','CST6CDT,M3.2.0,M11.1.0','America/Chicago America/Winnipeg'],
['US Mountain','MST7MDT,M3.2.0,M11.1.0','America/Denver America/Edmonton'],
['Arizona','MST7','America/Phoenix'],
['US Pacific','PST8PDT,M3.2.0,M11.1.0','America/Los_Angeles America/Vancouver'],
['Alaska','AKST9AKDT,M3.2.0,M11.1.0','America/Anchorage'],
['Hawaii','HST10','Pacific/Honolulu'],
['Brazil (São Paulo)','<-03>3','America/Sao_Paulo America/Argentina'],
];
function tzFromBrowser(){
  const z=Intl.DateTimeFormat().resolvedOptions().timeZone||'';
  let best=-1,len=0;
  TZ.forEach((t,i)=>t[2].split(' ').forEach(p=>{if(z.startsWith(p)&&p.length>len){best=i;len=p.length}}));
  return best;
}
function initDevice(d){
  $('tz').innerHTML=TZ.map((t,i)=>`<option value="${i}">${t[0]}</option>`).join('')+'<option value="c">Other (POSIX TZ)</option>';
  let i=TZ.findIndex(t=>t[1]==d.timezone);
  if(!d.configured&&tzFromBrowser()>=0)i=tzFromBrowser();
  $('tz').value=i>=0?i:'c';$('tzposix').value=d.timezone;$('tzcustom').hidden=i>=0;
  $('tz').onchange=()=>{$('tzcustom').hidden=$('tz').value!='c'};
  $('ssid').value=d.ssid;$('ssid2').value=d.ssid2;$('bip').value=d.printer_ip;$('bserial').value=d.printer_serial;$('bridge').value=d.bridge_url;
  const keep='unchanged';
  if(d.has_pass)$('pass').placeholder=keep;
  if(d.has_pass2)$('pass2').placeholder=keep;
  if(d.has_code)$('bcode').placeholder=keep;
  $('webpass').placeholder=d.has_web_pass?keep:'choose one (8+ characters)';
}
async function scan(){
  $('scanmsg').textContent='looking for networks...';
  for(let i=0;i<20;i++){
    const r=await (await fetch('/scan.json')).json();
    if(r.networks){
      // Strongest first, with a rough signal strength; picking one fills in the network box.
      const esc=t=>t.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');
      const bars=r=>r>-60?'strong':r>-75?'ok':'weak';
      $('nets').innerHTML=`<option value="">pick one (${r.networks.length})</option>`+
        r.networks.map(n=>`<option value="${esc(n.ssid)}">${esc(n.ssid)} · ${bars(n.rssi)}</option>`).join('');
      $('nets').value=r.networks.some(n=>n.ssid==$('ssid').value)?$('ssid').value:'';
      $('netrow').hidden=false;
      $('scanmsg').innerHTML=`<button class="ghost" type="button" onclick="scan()">Search again</button>`;
      return;
    }
    await new Promise(r=>setTimeout(r,1000));
  }
  $('scanmsg').innerHTML='No answer. <button class="ghost" type="button" onclick="scan()">Try again</button>';
}
function afterRestart(msg){
  $('devmsg').innerHTML=`<span class="ok">${msg}</span>`;
  if(!HOTSPOT)setTimeout(()=>location.reload(),15000);
}
async function saveDevice(){
  const tz=$('tz').value=='c'?$('tzposix').value:TZ[$('tz').value][1];
  $('devmsg').textContent='saving...';
  const r=await (await post('/device',{ssid:$('ssid').value,pass:$('pass').value,ssid2:$('ssid2').value,pass2:$('pass2').value,
    webpass:$('webpass').value,tz,bip:$('bip').value,bserial:$('bserial').value,bcode:$('bcode').value,bridge:$('bridge').value})).json();
  if(!r.ok){$('devmsg').innerHTML=`<span class="err">${r.error}</span>`;return}
  afterRestart(HOTSPOT?`Saved. The gauge now restarts and joins ${$('ssid').value}. Connect back to your own Wi-Fi and open http://desk-gauge.local/`
                       :'Saved, restarting...');
}
async function restart(){
  if(!confirm('Restart the gauge?'))return;
  await post('/restart');afterRestart('restarting...');
}
async function factoryReset(){
  if(!confirm('Erase everything on the gauge (Wi-Fi, page password, Claude sign-in, settings) and start over?'))return;
  await post('/reset');
  $('devmsg').innerHTML='<span class="ok">Erased. The gauge restarts with its setup hotspot; the details are on its screen.</span>';
}
function fmt(s){if(s<0)return'--';const d=s/86400|0,h=s%86400/3600|0,m=s%3600/60|0;return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m'}
async function load(first){
  const st=await (await fetch('/state.json')).json(), c=st.claude;
  if(first){
    HOTSPOT=st.device.via_hotspot;initDevice(st.device);
    if(HOTSPOT){
      // On the setup hotspot there is no internet: only the device setup is useful here.
      for(const id of ['faces','claude','settings','devbuttons'])$(id).hidden=true;
      $('welcome').hidden=false;scan();return;
    }
    FACES=st.faces;$('ver').textContent=st.version;
    $('faces').innerHTML=[...Array(FACES).keys()].map(i=>`<img id="f${i}" src="/screen/${i}.bmp">`).join('');
    $('theme').innerHTML=st.themes.map((t,i)=>`<option value="${i}">${t}</option>`).join('');
    $('poll').value=st.settings.poll_s;$('face').value=st.settings.face;
    $('theme').value=st.settings.theme;$('bright').value=st.settings.brightness;
    $('non').checked=st.settings.night_on;$('nstart').value=st.settings.night_start;$('nend').value=st.settings.night_end;$('nbright').value=st.settings.night_bright;$('saver').value=st.settings.saver_min;$('rot').value=st.settings.rotation;
  }
  $('signedout').hidden=c.logged_in;$('signedin').hidden=!c.logged_in;
  let s=c.logged_in?(c.valid?`<span class="ok">Signed in</span> · session ${Math.round(c.session.pct)}% · week ${Math.round(c.week.pct)}%`:'Signed in, waiting for data')
                  :'<span class="err">Not signed in</span>';
  if(c.logged_in)s+=` · next poll in ${c.next_poll_s}s`;
  if(c.error&&c.logged_in)s+=`<br><span class="err">${c.error}</span>`;
  $('status').innerHTML=s;
}
async function startLogin(){
  const w=window.open('about:blank');
  const r=await (await post('/login/start')).json();
  if(w)w.location=r.url;else location.href=r.url;
}
async function finishLogin(){
  $('loginmsg').textContent='connecting...';
  const r=await (await post('/login/finish',{code:$('code').value})).json();
  $('loginmsg').innerHTML=r.ok?'<span class="ok">connected</span>':`<span class="err">${r.error}</span>`;
  load();
}
async function save(){
  await post('/settings',{poll:$('poll').value,face:$('face').value,theme:$('theme').value,bright:$('bright').value,
    non:$('non').checked?1:0,nstart:$('nstart').value,nend:$('nend').value,nbright:$('nbright').value,saver:$('saver').value,rot:$('rot').value});
  $('savemsg').textContent='saved';setTimeout(()=>$('savemsg').textContent='',1500);
}
// Reload faces one at a time; the ESP renders on demand.
async function tick(){for(let i=0;i<FACES;i++){await new Promise(r=>{const n=new Image();
n.onload=n.onerror=()=>{const el=$('f'+i);if(el)el.src=n.src;r()};n.src='/screen/'+i+'.bmp?t='+Date.now()})}
setTimeout(tick,1500)}
load(true).then(()=>{if(!HOTSPOT){tick();setInterval(load,5000)}});
</script></body></html>)HTML";

static void handleScreen(int index) {
  LGFX_Sprite spr;
  spr.setColorDepth(psramFound() ? 16 : 8);  // 8-bit keeps the no-PSRAM build within heap
  spr.setPsram(psramFound());
  if (!spr.createSprite(PANEL_W, PANEL_H)) {
    server.send(503, "text/plain", "out of memory");
    return;
  }
  // ?splash=<ms> or ?saver=<ms> shows that moment of the boot splash or screensaver instead.
  if (server.hasArg("splash")) ui::drawSplash(spr, server.arg("splash").toInt());
  else if (server.hasArg("saver")) {
    ui::drawClaude(spr, g_claude);
    ui::drawSaver(spr, nullptr, server.arg("saver").toInt());
  }
  else renderFace(index, spr);

  const int w = spr.width(), h = spr.height();
  const uint32_t rowBytes = (w * 3 + 3) & ~3u;
  const uint32_t imageSize = rowBytes * h, fileSize = 54 + imageSize;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
  put32(2, fileSize);
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);  // positive height = bottom-up rows
  hdr[26] = 1;
  hdr[28] = 24;
  put32(34, imageSize);

  WiFiClient client = server.client();
  server.setContentLength(fileSize);
  server.send(200, "image/bmp", "");
  client.write(hdr, sizeof hdr);

  uint8_t* row = (uint8_t*)malloc(rowBytes);
  if (!row) return;
  memset(row, 0, rowBytes);
  for (int y = h - 1; y >= 0; --y) {
    spr.readRectRGB(0, y, w, 1, row);  // RGB888
    for (int x = 0; x < w; ++x) std::swap(row[x * 3], row[x * 3 + 2]);  // BMP wants BGR
    client.write(row, rowBytes);
  }
  free(row);
}

// The request came in over the setup hotspot (whoever joined it can read its password off the screen).
static bool viaHotspot() {
  return (WiFi.getMode() & WIFI_AP) && server.client().localIP() == WiFi.softAPIP();
}

static void putWindow(JsonObject o, const UsageWindow& w) {
  o["pct"] = w.pct;
  o["resets_in"] = w.remaining();
  o["resets_at"] = (long)w.resetsAt;
  if (w.label[0]) o["label"] = w.label;
}

static void handleState() {
  JsonDocument doc;
  const ClaudeUsage& cu = g_claude;
  JsonObject c = doc["claude"].to<JsonObject>();
  c["logged_in"] = cu.loggedIn;
  c["valid"] = cu.valid;
  putWindow(c["session"].to<JsonObject>(), cu.session);
  putWindow(c["week"].to<JsonObject>(), cu.week);
  JsonArray sc = c["scoped"].to<JsonArray>();
  for (int i = 0; i < cu.scopedCount; ++i) putWindow(sc.add<JsonObject>(), cu.scoped[i]);
  if (!isnan(cu.sessionRatePerH)) c["session_rate_per_h"] = cu.sessionRatePerH;
  c["session_to_limit_s"] = cu.sessionToLimitS;
  if (!isnan(cu.weekProjectedPct)) c["week_projected_pct"] = cu.weekProjectedPct;
  c["spike"] = cu.spike;
  c["fetched_at"] = (long)cu.fetchedAt;
  c["next_poll_s"] = cu.nextPollInS;
  c["error"] = cu.error;
  if (cu.bridgeOk) {
    c["today_tokens"] = cu.todayTokens;
    c["tok_per_min"] = cu.tokPerMin;
  }

  if (device::printerOn()) {
    JsonObject p = doc["printer"].to<JsonObject>();
    p["online"] = g_printer.online;
    p["state"] = g_printer.gcodeState;
    p["job"] = g_printer.job;
    p["percent"] = g_printer.percent;
    p["remaining_min"] = g_printer.remainingMin;
    p["layer"] = g_printer.layer;
    p["total_layers"] = g_printer.totalLayers;
    p["nozzle"] = g_printer.nozzle;
    p["bed"] = g_printer.bed;
    p["chamber"] = g_printer.chamber;
    p["print_error"] = g_printer.printError;
  }

  // Device setup, without the passwords.
  JsonObject d = doc["device"].to<JsonObject>();
  d["configured"] = device::configured();
  d["via_hotspot"] = viaHotspot();
  d["ssid"] = g_device.ssid;
  d["has_pass"] = g_device.pass.length() > 0;
  d["ssid2"] = g_device.ssid2;
  d["has_pass2"] = g_device.pass2.length() > 0;
  d["has_web_pass"] = g_device.webPass.length() > 0;
  d["timezone"] = g_device.timezone;
  d["printer_ip"] = g_device.bambuIp;
  d["printer_serial"] = g_device.bambuSerial;
  d["has_code"] = g_device.bambuCode.length() > 0;
  d["bridge_url"] = g_device.bridgeUrl;
  if (WiFi.status() == WL_CONNECTED) d["ip"] = WiFi.localIP().toString();

  JsonObject s = doc["settings"].to<JsonObject>();
  s["poll_s"] = g_settings.pollS;
  s["face"] = (int)g_settings.face;
  s["theme"] = g_settings.theme;
  s["brightness"] = g_settings.brightness;
  char hm[6];
  s["night_on"] = g_settings.nightOn;
  snprintf(hm, sizeof hm, "%02d:%02d", g_settings.nightStart / 60, g_settings.nightStart % 60);
  s["night_start"] = hm;
  snprintf(hm, sizeof hm, "%02d:%02d", g_settings.nightEnd / 60, g_settings.nightEnd % 60);
  s["night_end"] = hm;
  s["night_bright"] = g_settings.nightBright;
  s["night_now"] = settings::isNight();
  s["saver_min"] = g_settings.saverMin;
  s["rotation"] = g_settings.rotation;
  JsonArray themes = doc["themes"].to<JsonArray>();
  for (int i = 0; i < ui::themeCount(); ++i) themes.add(ui::themeName(i));
  doc["faces"] = faces;
  doc["version"] = FW_VERSION;
  JsonObject perf = doc["perf"].to<JsonObject>();
  perf["frames"] = g_perf.frames;
  perf["render_ms"] = g_perf.frames ? g_perf.renderMs / g_perf.frames : 0;
  perf["push_ms"] = g_perf.frames ? g_perf.pushMs / g_perf.frames : 0;
  perf["gap_ms"] = g_perf.frames > 1 ? g_perf.gapMs / (g_perf.frames - 1) : 0;
  perf["fps"] = g_perf.totalMs ? g_perf.frames * 1000.0f / g_perf.totalMs : 0;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_s"] = millis() / 1000;

  String out;
  serializeJsonPretty(doc, out);
  server.send(200, "application/json", out);
}

static void sendJson(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleLoginStart() {
  JsonDocument doc;
  doc["url"] = claude_auth::startLogin();
  sendJson(doc);
}

static void handleLoginFinish() {
  JsonDocument doc;
  String err;
  doc["ok"] = claude_auth::finishLogin(server.arg("code"), err);
  if (doc["ok"]) claude_source::pollNow();
  else doc["error"] = err;
  sendJson(doc);
}

static void handleSettings() {
  if (server.hasArg("poll")) g_settings.pollS = constrain(server.arg("poll").toInt(), CLAUDE_POLL_MIN_S, CLAUDE_POLL_MAX_S);
  if (server.hasArg("face")) g_settings.face = (FaceMode)constrain(server.arg("face").toInt(), 0, 2);
  if (server.hasArg("theme")) g_settings.theme = constrain(server.arg("theme").toInt(), 0, ui::themeCount() - 1);
  if (server.hasArg("bright")) g_settings.brightness = constrain(server.arg("bright").toInt(), 10, 255);
  // "HH:MM" from the time inputs -> minutes after midnight; anything else leaves the value alone.
  auto minutes = [](const String& v, uint16_t& out) {
    int h, m;
    if (sscanf(v.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) out = h * 60 + m;
  };
  if (server.hasArg("non")) g_settings.nightOn = server.arg("non") == "1";
  if (server.hasArg("nstart")) minutes(server.arg("nstart"), g_settings.nightStart);
  if (server.hasArg("nend")) minutes(server.arg("nend"), g_settings.nightEnd);
  if (server.hasArg("saver")) g_settings.saverMin = constrain(server.arg("saver").toInt(), 0, 1440);
  if (server.hasArg("nbright")) g_settings.nightBright = constrain(server.arg("nbright").toInt(), 0, 255);
  if (server.hasArg("rot")) g_settings.rotation = server.arg("rot").toInt() & 3;
  settings::save();
  server.send(204);
}

// Wi-Fi networks in range, strongest first. The scan runs in the background; until it is done the
// answer is {"scanning":true} and the page asks again.
static void handleScan() {
  JsonDocument doc;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
  if (n < 0) {
    doc["scanning"] = true;
    sendJson(doc);
    return;
  }
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {  // results come strongest first; keep one entry per name
    String ssid = WiFi.SSID(i);
    bool seen = !ssid.length();
    for (JsonObject o : nets) seen |= o["ssid"] == ssid;
    if (seen) continue;
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

// Saves the device setup and restarts. Empty password fields keep the stored one, as long as the
// network they belong to stays the same.
static void handleDevice() {
  JsonDocument doc;
  String ssid = server.arg("ssid"), ssid2 = server.arg("ssid2"), webPass = server.arg("webpass");
  ssid.trim();
  ssid2.trim();
  if (!ssid.length()) doc["error"] = "Pick a Wi-Fi network";
  else if (!webPass.length() && !g_device.webPass.length()) doc["error"] = "Choose a page password";
  else if (webPass.length() && webPass.length() < 8) doc["error"] = "The page password needs 8 or more characters";
  else if (server.arg("pass").length() && server.arg("pass").length() < 8) doc["error"] = "Wi-Fi passwords are 8 or more characters";
  if (!doc["error"].isNull()) {
    doc["ok"] = false;
    sendJson(doc);
    return;
  }
  auto secret = [](const char* field, String& stored, bool keepable) {
    String v = server.arg(field);
    if (v.length() || !keepable) stored = v;
  };
  secret("pass", g_device.pass, ssid == g_device.ssid);
  secret("pass2", g_device.pass2, ssid2 == g_device.ssid2 && ssid2.length());
  secret("webpass", g_device.webPass, true);
  g_device.ssid = ssid;
  g_device.ssid2 = ssid2;
  String tz = server.arg("tz");
  tz.trim();
  if (tz.length()) g_device.timezone = tz;
  g_device.bambuIp = server.arg("bip");
  g_device.bambuIp.trim();
  g_device.bambuSerial = server.arg("bserial");
  g_device.bambuSerial.trim();
  secret("bcode", g_device.bambuCode, g_device.bambuIp.length());
  g_device.bridgeUrl = server.arg("bridge");
  g_device.bridgeUrl.trim();
  device::save();
  doc["ok"] = true;
  sendJson(doc);
  restartAt = millis() + 1500;
}

// Every page needs the web login (HTTP Basic), except over the setup hotspot. After WEB_MAX_FAILS wrong
// attempts in a row the page refuses all requests for WEB_LOCKOUT_MS.
static uint8_t failedLogins = 0;
static uint32_t lockedUntil = 0;

static bool authorized() {
  if (viaHotspot()) return true;
  if (lockedUntil && (int32_t)(millis() - lockedUntil) < 0) {
    server.send(429, "text/plain", "Too many wrong logins, try again in a minute");
    return false;
  }
  lockedUntil = 0;
  if (g_device.webPass.length() && server.authenticate(WEB_USER, g_device.webPass.c_str())) {
    failedLogins = 0;
    return true;
  }
  if (server.hasHeader("Authorization") && ++failedLogins >= WEB_MAX_FAILS) {
    failedLogins = 0;
    lockedUntil = millis() + WEB_LOCKOUT_MS;
    Serial.println("[web] too many wrong logins, locked");
  }
  server.requestAuthentication(BASIC_AUTH, HOSTNAME);
  return false;
}

// Changes also need the page's own X-Gauge header, which a form on another site cannot send.
// On the setup hotspot, requests for any other host (a phone checking for internet) are sent to the
// setup page, which makes the phone open it by itself.
static bool captiveRedirect() {
  if (!viaHotspot() || server.hostHeader() == WiFi.softAPIP().toString()) return false;
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
  return true;
}

static WebServer::THandlerFunction guarded(WebServer::THandlerFunction fn, bool change = false) {
  return [fn, change] {
    if (captiveRedirect() || !authorized()) return;
    if (change && server.header("X-Gauge") != "1") {
      server.send(403, "text/plain", "Forbidden");
      return;
    }
    fn();
  };
}

void begin(RenderFn render, int faceCount, ActionFn replay, ActionFn saver) {
  renderFace = render;
  replaySplash = replay;
  playSaver = saver;
  faces = faceCount;
  static const char* kHeaders[] = {"Authorization", "X-Gauge"};
  server.collectHeaders(kHeaders, 2);
  server.on("/", guarded([] { server.send_P(200, "text/html", kIndex); }));
  server.on("/screen/0.bmp", guarded([] { handleScreen(0); }));
  server.on("/screen/1.bmp", guarded([] { handleScreen(min(1, faces - 1)); }));
  server.on("/state.json", guarded(handleState));
  server.on("/login/start", HTTP_POST, guarded(handleLoginStart, true));
  server.on("/login/finish", HTTP_POST, guarded(handleLoginFinish, true));
  server.on("/logout", HTTP_POST, guarded([] {
    claude_auth::logout();
    claude_source::forget();
    server.send(204);
  }, true));
  server.on("/poll", HTTP_POST, guarded([] {
    claude_source::pollNow();
    server.send(204);
  }, true));
  server.on("/settings", HTTP_POST, guarded(handleSettings, true));
  server.on("/scan.json", guarded(handleScan));
  server.on("/device", HTTP_POST, guarded(handleDevice, true));
  server.on("/restart", HTTP_POST, guarded([] {
    server.send(204);
    restartAt = millis() + 500;
  }, true));
  server.on("/reset", HTTP_POST, guarded([] {
    server.send(204);
    delay(200);
    device::factoryReset();
  }, true));
  server.on("/saver", HTTP_POST, guarded([] {
    if (playSaver) playSaver();
    server.send(204);
  }, true));
  server.on("/splash", HTTP_POST, guarded([] {
    if (replaySplash) replaySplash();
    server.send(204);
  }, true));
  server.onNotFound(guarded([] { server.send(404, "text/plain", "Not found"); }));
  server.begin();
}

void loop() {
  server.handleClient();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
}

}  // namespace web
