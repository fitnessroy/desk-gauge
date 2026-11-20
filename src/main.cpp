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
// Sends only the rows that changed since the last frame (`prev` is a copy in PSRAM), as separate
// bands when the changes are far apart, so small animations cost strips instead of whole frames.
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
  const int kMergeGap = 8;  // unchanged rows worth resending to save a window setup
  auto changed = [&](int y) { return memcmp(src + y * PANEL_W, prev + y * PANEL_W, row) != 0; };
  for (int y = 0; y < PANEL_H;) {
    if (!changed(y)) {
      ++y;
      continue;
    }
    int y0 = y, y1 = y, quiet = 0;
    while (++y < PANEL_H && quiet <= kMergeGap) {
      if (changed(y)) y1 = y, quiet = 0;
      else ++quiet;
    }
    y = y1 + 1;
    panel.pushRows(src, y0, y1);
    memcpy(prev + y0 * PANEL_W, src + y0 * PANEL_W, (y1 - y0 + 1) * row);
  }
}
static uint16_t* prev0 = nullptr;
#if ENABLE_PRINTER
static uint16_t* prev1 = nullptr;
#endif

// The boot splash runs as its own task so Wi-Fi, NTP and the web server start underneath it.
// While it runs it owns panel0/frame0, and loop() leaves the screen alone.
static volatile bool splashRunning = false;
static void splashTask(void*) {
  uint32_t t0 = millis();
  for (uint32_t t; (t = millis() - t0) < SPLASH_MS;) {
    ui::drawSplash(frame0, t);
    pushFrame(panel0, frame0, prev0);
    vTaskDelay(1);
  }
  splashRunning = false;
  vTaskDelete(nullptr);
}
#endif

// The screensaver runs in loop() itself (it reads g_claude, which only loop() writes):
// every saverMin minutes, or from "Play screensaver" on the web page.
static bool saverOn = false;
static uint32_t saverStart = 0, saverDone = 0;
static void startSaver() {
#if HAS_PANELS
  if (splashRunning || saverOn) return;
  saverOn = true;
  saverStart = millis();
#endif
}

// Plays the boot splash (at boot, and from "Replay intro" on the web page).
static void startSplash() {
#if HAS_PANELS
  if (splashRunning) return;
  splashRunning = true;
  xTaskCreatePinnedToCore(splashTask, "splash", 8192, nullptr, 1, nullptr, 1);
#endif
}

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
// Eases the backlight toward its target (day brightness, or the night level while night mode
// applies) instead of jumping, so night falls over about a second.
static void applyBrightness() {
  static int applied = -1;
  static uint32_t lastStep = 0;
  int target = settings::isNight() ? g_settings.nightBright : g_settings.brightness;
  if (applied == target) return;
  if (applied < 0) applied = target;
  else if (millis() - lastStep < 15) return;
  else applied += constrain(target - applied, -4, 4);
  lastStep = millis();
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
  startSplash();

  connectWifi();
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");  // TLS and reset times need the clock
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
#if ENABLE_PRINTER
  bambu_source::begin();
#endif
  web::begin(renderFace, kFaces, startSplash, startSaver);

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
  applyBrightness();
  // Pointless with the backlight off at night, and it waits until there is a dashboard to wipe.
  if (g_settings.saverMin && !saverOn && !splashRunning && g_claude.valid &&
      !(settings::isNight() && g_settings.nightBright == 0) &&
      millis() - saverDone >= g_settings.saverMin * 60000UL)
    startSaver();
  if (saverOn) {
    uint32_t t = millis() - saverStart;
    if (t < SAVER_MS) {
      ui::drawSaver(frame0, g_claude, t);
      pushFrame(panel0, frame0, prev0);
    } else {
      saverOn = false;
      saverDone = millis();
    }
  }
  static uint32_t lastRender = 0;
  if (!splashRunning && !saverOn && millis() - lastRender >= (ui::animated(g_claude) ? RENDER_ANIM_MS : RENDER_MS)) {
    lastRender = millis();
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
