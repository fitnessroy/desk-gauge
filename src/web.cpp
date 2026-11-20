#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "claude_auth.h"
#include "claude_source.h"
#include "config.h"
#include "model.h"
#include "settings.h"
#include "ui.h"

namespace web {

static WebServer server(80);
static RenderFn renderFace;
static ActionFn replaySplash;
static int faces = 1;

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
.muted{color:#888;font-size:13px} .err{color:#eb5046} .ok{color:#6ec878}
a{color:#d97757}
</style></head><body>
<div class="row" id="faces"></div>

<section>
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

<section>
<h2>Settings</h2>
<label>Poll every <span><input type="number" id="poll" min="120" max="3600" style="width:80px"> s</span></label>
<label>Face <select id="face"><option value="0">Rings</option><option value="1">Bars</option><option value="2">Rotate</option></select></label>
<label>Theme <select id="theme"></select></label>
<label>Brightness <input type="range" id="bright" min="10" max="255"></label>
<label>Night mode <input type="checkbox" id="non"></label>
<label>Night from <span><input type="time" id="nstart"> to <input type="time" id="nend"></span></label>
<label>Night brightness <span><input type="range" id="nbright" min="0" max="255"> <small class="muted">left = screen off</small></span></label>
<p><button onclick="save()">Save</button> <span id="savemsg" class="muted"></span></p>
<p><button class="ghost" onclick="post('/splash')">Replay intro</button></p>
</section>

<small><a href="/state.json">state.json</a></small>
<script>
const $=id=>document.getElementById(id);
const post=(u,body)=>fetch(u,{method:'POST',headers:{'X-Gauge':'1'},body:body?new URLSearchParams(body):undefined});
let FACES=1;
function fmt(s){if(s<0)return'--';const d=s/86400|0,h=s%86400/3600|0,m=s%3600/60|0;return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m'}
async function load(first){
  const st=await (await fetch('/state.json')).json(), c=st.claude;
  if(first){
    FACES=st.faces;
    $('faces').innerHTML=[...Array(FACES).keys()].map(i=>`<img id="f${i}" src="/screen/${i}.bmp">`).join('');
    $('theme').innerHTML=st.themes.map((t,i)=>`<option value="${i}">${t}</option>`).join('');
    $('poll').value=st.settings.poll_s;$('face').value=st.settings.face;
    $('theme').value=st.settings.theme;$('bright').value=st.settings.brightness;
    $('non').checked=st.settings.night_on;$('nstart').value=st.settings.night_start;$('nend').value=st.settings.night_end;$('nbright').value=st.settings.night_bright;
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
    non:$('non').checked?1:0,nstart:$('nstart').value,nend:$('nend').value,nbright:$('nbright').value});
  $('savemsg').textContent='saved';setTimeout(()=>$('savemsg').textContent='',1500);
}
// Reload faces one at a time; the ESP renders on demand.
async function tick(){for(let i=0;i<FACES;i++){await new Promise(r=>{const n=new Image();
n.onload=n.onerror=()=>{const el=$('f'+i);if(el)el.src=n.src;r()};n.src='/screen/'+i+'.bmp?t='+Date.now()})}
setTimeout(tick,1500)}
load(true).then(tick);setInterval(load,5000);
</script></body></html>)HTML";

static void handleScreen(int index) {
  LGFX_Sprite spr;
  spr.setColorDepth(psramFound() ? 16 : 8);  // 8-bit keeps the no-PSRAM build within heap
  spr.setPsram(psramFound());
  if (!spr.createSprite(PANEL_W, PANEL_H)) {
    server.send(503, "text/plain", "out of memory");
    return;
  }
  // ?splash=<ms> shows that moment of the boot splash instead, for working on it.
  if (server.hasArg("splash")) ui::drawSplash(spr, server.arg("splash").toInt());
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

#if ENABLE_PRINTER
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
#endif

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
  JsonArray themes = doc["themes"].to<JsonArray>();
  for (int i = 0; i < ui::themeCount(); ++i) themes.add(ui::themeName(i));
  doc["faces"] = faces;
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
  if (server.hasArg("nbright")) g_settings.nightBright = constrain(server.arg("nbright").toInt(), 0, 255);
  settings::save();
  server.send(204);
}

// Every page needs the web login from secrets.h (HTTP Basic). After WEB_MAX_FAILS wrong
// attempts in a row the page refuses all requests for WEB_LOCKOUT_MS.
static uint8_t failedLogins = 0;
static uint32_t lockedUntil = 0;

static bool authorized() {
  if (lockedUntil && (int32_t)(millis() - lockedUntil) < 0) {
    server.send(429, "text/plain", "Too many wrong logins, try again in a minute");
    return false;
  }
  lockedUntil = 0;
  if (server.authenticate(WEB_USER, WEB_PASS)) {
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
static WebServer::THandlerFunction guarded(WebServer::THandlerFunction fn, bool change = false) {
  return [fn, change] {
    if (!authorized()) return;
    if (change && server.header("X-Gauge") != "1") {
      server.send(403, "text/plain", "Forbidden");
      return;
    }
    fn();
  };
}

void begin(RenderFn render, int faceCount, ActionFn replay) {
  renderFace = render;
  replaySplash = replay;
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
  server.on("/splash", HTTP_POST, guarded([] {
    if (replaySplash) replaySplash();
    server.send(204);
  }, true));
  server.onNotFound(guarded([] { server.send(404, "text/plain", "Not found"); }));
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace web
