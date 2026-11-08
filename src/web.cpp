#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "config.h"
#include "model.h"

namespace web {

static WebServer server(80);
static RenderFn renderFace;

static const char kIndex[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Desk Gauge</title>
<style>
body{background:#111;color:#ccc;font-family:system-ui,sans-serif;display:flex;flex-direction:column;
align-items:center;gap:16px;padding:24px;margin:0}
.row{display:flex;gap:32px;flex-wrap:wrap;justify-content:center}
img{width:240px;height:240px;border-radius:50%;box-shadow:0 0 0 10px #222,0 0 30px #000}
</style></head><body>
<div class="row"><img id="f0" src="/screen/0.bmp"><img id="f1" src="/screen/1.bmp"></div>
<small><a style="color:#888" href="/state.json">state.json</a></small>
<script>
// Reload faces one at a time; the ESP renders on demand.
async function tick(){for(const i of [0,1]){await new Promise(r=>{const n=new Image();
n.onload=n.onerror=()=>{document.getElementById('f'+i).src=n.src;r()};n.src='/screen/'+i+'.bmp?t='+Date.now()})}
setTimeout(tick,1500)}
tick();
</script></body></html>)HTML";

static void handleScreen(int index) {
  LGFX_Sprite spr;
  spr.setColorDepth(psramFound() ? 16 : 8);  // 8-bit keeps the no-PSRAM build within heap
  spr.setPsram(psramFound());
  if (!spr.createSprite(PANEL_W, PANEL_H)) {
    server.send(503, "text/plain", "out of memory");
    return;
  }
  renderFace(index, spr);

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

static void handleState() {
  JsonDocument doc;
  JsonObject c = doc["claude"].to<JsonObject>();
  c["valid"] = g_claude.valid;
  c["session_pct"] = g_claude.session.pct;
  c["session_resets_in"] = g_claude.remaining(g_claude.session);
  c["week_pct"] = g_claude.week.pct;
  c["week_resets_in"] = g_claude.remaining(g_claude.week);
  c["today_tokens"] = g_claude.todayTokens;
  c["error"] = g_claude.error;

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

  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_s"] = millis() / 1000;

  String out;
  serializeJsonPretty(doc, out);
  server.send(200, "application/json", out);
}

void begin(RenderFn render) {
  renderFace = render;
  server.on("/", [] { server.send_P(200, "text/html", kIndex); });
  server.on("/screen/0.bmp", [] { handleScreen(0); });
  server.on("/screen/1.bmp", [] { handleScreen(1); });
  server.on("/state.json", handleState);
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace web
