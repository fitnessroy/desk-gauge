#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "bambu_source.h"
#include "claude_auth.h"
#include "claude_source.h"
#include "config.h"
#include "model.h"
#include "settings.h"
#include "ui.h"
#include "web.h"

static constexpr int kFaces = ENABLE_PRINTER ? 2 : 1;

#if HAS_PANELS
#include "panels.h"
static RoundPanel panel0(SPI2_HOST, PanelPins PANEL0_PINS, 6);
static LGFX_Sprite frame0;
#if ENABLE_PRINTER
static RoundPanel panel1(SPI3_HOST, PanelPins PANEL1_PINS, 7);
static LGFX_Sprite frame1;
#endif
#endif

ClaudeUsage g_claude;
PrinterState g_printer;

#if HAS_PANELS
// Sends only the rows that changed since the last frame (`prev` is a copy in PSRAM), so a small
// animation costs a strip of the screen instead of the whole frame.
static void pushFrame(RoundPanel& panel, LGFX_Sprite& frame, uint16_t*& prev) {
  const auto* src = (const uint16_t*)frame.getBuffer();
  if (!src) return;
  if (!prev) {
    prev = (uint16_t*)ps_malloc(PANEL_W * PANEL_H * 2);
    if (!prev) return;
    panel.pushFrame(src);
    memcpy(prev, src, PANEL_W * PANEL_H * 2);
    return;
  }
  const size_t row = PANEL_W * 2;
  int y0 = 0, y1 = PANEL_H - 1;
  while (y0 < PANEL_H && !memcmp(src + y0 * PANEL_W, prev + y0 * PANEL_W, row)) ++y0;
  if (y0 == PANEL_H) return;
  while (y1 > y0 && !memcmp(src + y1 * PANEL_W, prev + y1 * PANEL_W, row)) --y1;
  panel.pushRows(src, y0, y1);
  memcpy(prev + y0 * PANEL_W, src + y0 * PANEL_W, (y1 - y0 + 1) * row);
}
static uint16_t* prev0 = nullptr;
#if ENABLE_PRINTER
static uint16_t* prev1 = nullptr;
#endif
#endif

static void renderFace(int index, LGFX_Sprite& s) {
  if (index == 0) ui::drawClaude(s, g_claude);
  else ui::drawPrinter(s, g_printer);
}

// Networks in priority order: WIFI_SSID first, WIFI_SSID2 only while the first is not in range.
struct WifiNet {
  const char* ssid;
  const char* pass;
};
static const WifiNet kNets[] = {
    {WIFI_SSID, WIFI_PASS},
#ifdef WIFI_SSID2
    {WIFI_SSID2, WIFI_PASS2},
#endif
};
static constexpr int kNetCount = sizeof kNets / sizeof kNets[0];

// Index of the highest-priority network in range, -1 if none, -2 if the scan failed.
static int bestNetworkInRange() {
  int n = WiFi.scanNetworks();
  if (n < 0) return -2;
  int best = -1;
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < kNetCount; ++k)
      if (WiFi.SSID(i) == kNets[k].ssid && (best < 0 || k < best)) best = k;
  WiFi.scanDelete();
  return best;
}

static bool joinNetwork(int k, uint32_t timeoutMs) {
  Serial.printf("[wifi] connecting to %s\n", kNets[k].ssid);
  WiFi.disconnect();
  WiFi.begin(kNets[k].ssid, kNets[k].pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("[wifi] %s, ip %s  ->  http://%s.local/\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), HOSTNAME);
  return true;
}

// Joins the best network in range, falling back down the list if joining fails.
static bool joinBest(uint32_t timeoutMs) {
  int best = bestNetworkInRange();
  if (best == -1) return false;
  for (int k = best < 0 ? 0 : best; k < kNetCount; ++k)
    if (joinNetwork(k, timeoutMs)) return true;
  return false;
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  for (int i = 0; i < 3 && !joinBest(10000); ++i) {
  }
  if (WiFi.status() != WL_CONNECTED) Serial.println("[wifi] not connected yet, will keep retrying");
}

// While offline, rescan every WIFI_RETRY_MS. While on a fallback network, look for the first
// network every WIFI_PREFER_MS and move back to it once it is in range.
static void maintainWifi() {
  static uint32_t lastTry = 0, lastPreferCheck = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastTry < WIFI_RETRY_MS) return;
    lastTry = millis();
    joinBest(8000);
    return;
  }
  if (kNetCount < 2 || WiFi.SSID() == kNets[0].ssid || millis() - lastPreferCheck < WIFI_PREFER_MS) return;
  lastPreferCheck = millis();
  if (bestNetworkInRange() == 0 && !joinNetwork(0, 10000)) joinBest(8000);
}

static void logState() {
  Serial.printf("[claude] signed_in=%d valid=%d 5h=%.0f%% (%ds) week=%.0f%% (%ds) scoped=%d next=%us err='%s'\n",
                g_claude.loggedIn, g_claude.valid, g_claude.session.pct, g_claude.session.remaining(),
                g_claude.week.pct, g_claude.week.remaining(), g_claude.scopedCount, g_claude.nextPollInS,
                g_claude.error.c_str());
#if ENABLE_PRINTER
  Serial.printf("[printer] online=%d %s %d%% %dmin layer %d/%d N%.0f B%.0f\n", g_printer.online,
                g_printer.gcodeState.c_str(), g_printer.percent, g_printer.remainingMin, g_printer.layer,
                g_printer.totalLayers, g_printer.nozzle, g_printer.bed);
#endif
  Serial.printf("[sys] heap=%u\n", ESP.getFreeHeap());
}

#if HAS_PANELS
static void applyBrightness() {
  static int applied = -1;
  if (applied == g_settings.brightness) return;
  applied = g_settings.brightness;
  panel0.setBrightness(applied);
#if ENABLE_PRINTER
  panel1.setBrightness(applied);
#endif
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== desk-gauge ===");
  settings::load();
  claude_auth::begin();
  claude_source::begin();

#if HAS_PANELS
  struct {
    RoundPanel* panel;
    LGFX_Sprite* frame;
  } screens[] = {{&panel0, &frame0},
#if ENABLE_PRINTER
                 {&panel1, &frame1}
#endif
  };
  for (auto& sc : screens) {
    if (!sc.panel->init()) Serial.println("[panel] init failed");
    sc.panel->fill(0x0000);
    sc.frame->setColorDepth(16);
    sc.frame->setPsram(true);
    sc.frame->createSprite(PANEL_W, PANEL_H);
  }
  applyBrightness();
#endif

  connectWifi();
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");  // TLS and reset times need the clock
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
#if ENABLE_PRINTER
  bambu_source::begin();
#endif
  web::begin(renderFace, kFaces);

  // Firmware updates over Wi-Fi (pio run -e esp32s3_ota -t upload), same password as the web page.
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(WEB_PASS);
  ArduinoOTA.setMdnsEnabled(false);  // mDNS is already running above
  ArduinoOTA.onStart([] { Serial.println("[ota] update started"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] failed (%u)\n", e); });
  ArduinoOTA.begin();
}

void loop() {
  maintainWifi();
  ArduinoOTA.handle();
#if ENABLE_PRINTER
  bambu_source::loop();
#endif
  claude_source::loop();
  web::loop();

#if HAS_PANELS
  static uint32_t lastRender = 0;
  if (millis() - lastRender >= (ui::animated(g_claude) ? RENDER_ANIM_MS : RENDER_MS)) {
    lastRender = millis();
    applyBrightness();
    renderFace(0, frame0);
    pushFrame(panel0, frame0, prev0);
#if ENABLE_PRINTER
    renderFace(1, frame1);
    pushFrame(panel1, frame1, prev1);
#endif
  }
#endif

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= SERIAL_LOG_MS) {
    lastLog = millis();
    logState();
  }
  delay(2);
}
