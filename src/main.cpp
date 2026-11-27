#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "bambu_source.h"
#include "claude_auth.h"
#include "claude_source.h"
#include "config.h"
#include "device.h"
#include "model.h"
#include "settings.h"
#include "ui.h"
#include "web.h"

// The printer face (and the second screen) is there when a printer is set up.
static bool printer = false;

#if HAS_PANELS
#include "panels.h"
static RoundPanel panel0(SPI2_HOST, PanelPins PANEL0_PINS, 6);
static LGFX_Sprite frame0;
static RoundPanel panel1(SPI3_HOST, PanelPins PANEL1_PINS, 7);
static LGFX_Sprite frame1;
#endif

ClaudeUsage g_claude;
FramePerf g_perf;
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
static uint16_t* prev1 = nullptr;

// Orientation from the web page. The sprites turn their drawing coordinates, so the buffer that
// goes to the panel comes out rotated; the web preview renders into its own sprite and stays upright.
static void applyRotation() {
  frame0.setRotation(g_settings.rotation);
  if (printer) frame1.setRotation(g_settings.rotation);
}

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

// The screensaver runs as its own task like the splash, every saverMin minutes or from "Play
// screensaver" on the web page. The dashboard is drawn once, in loop()'s thread (the only one that
// touches g_claude), and the task paints the wipe over a copy of it each frame.
static volatile bool saverOn = false;
static uint32_t saverDone = 0;
static uint16_t* saverBase = nullptr;
#if HAS_PANELS
static void saverTask(void*) {
  uint32_t start = millis();
  g_perf = FramePerf();
  for (uint32_t t; (t = millis() - start) < SAVER_MS;) {
    uint32_t t0 = millis();
    if (g_perf.lastEnd) g_perf.gapMs += t0 - g_perf.lastEnd;
    ui::drawSaver(frame0, saverBase, t);
    uint32_t t1 = millis();
    pushFrame(panel0, frame0, prev0);
    g_perf.frames++;
    g_perf.renderMs += t1 - t0;
    g_perf.pushMs += millis() - t1;
    g_perf.totalMs = t;
    g_perf.lastEnd = millis();
    vTaskDelay(1);
  }
  saverDone = millis();
  saverOn = false;
  vTaskDelete(nullptr);
}
#endif

static void startSaver() {
#if HAS_PANELS
  if (splashRunning || saverOn) return;
  if (!saverBase) saverBase = (uint16_t*)ps_malloc(PANEL_W * PANEL_H * 2);
  if (!saverBase) return;
  applyRotation();
  ui::drawClaude(frame0, g_claude);
  memcpy(saverBase, frame0.getBuffer(), PANEL_W * PANEL_H * 2);
  saverOn = true;
  // Above loop() so a busy pass there (TLS, web) doesn't stall frames; it yields every frame.
  xTaskCreatePinnedToCore(saverTask, "saver", 8192, nullptr, 2, nullptr, 1);
#endif
}

// Plays the boot splash (at boot, and from "Replay intro" on the web page).
static void startSplash() {
#if HAS_PANELS
  if (splashRunning) return;
  applyRotation();
  splashRunning = true;
  xTaskCreatePinnedToCore(splashTask, "splash", 8192, nullptr, 1, nullptr, 1);
#endif
}

// Setup hotspot (see SETUP_AP_* in config.h). Its password is new each time and shown only on the
// screen, so whoever joins it has the gauge in front of them; the web page trusts them without a login.
static DNSServer dns;
static bool apOn = false;
static uint32_t apSince = 0;
static String apSsid, apPass;

static void startSetupAp() {
  if (apOn) return;
  char id[5];
  snprintf(id, sizeof id, "%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
  apSsid = String(SETUP_AP_PREFIX) + id;
  static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";  // nothing to mix up with 1/l/0/o
  apPass = "";
  for (int i = 0; i < 8; ++i) apPass += kChars[esp_random() % (sizeof kChars - 1)];
  WiFi.mode(WIFI_AP_STA);  // the station side keeps scanning and joining the home network
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());  // every name points here, so phones pop up the setup page
  apOn = true;
  apSince = millis();
  Serial.printf("[setup] hotspot %s, password %s, http://%s/\n", apSsid.c_str(), apPass.c_str(),
                WiFi.softAPIP().toString().c_str());
}

static void stopSetupAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  Serial.println("[setup] hotspot closed");
}

static void renderFace(int index, LGFX_Sprite& s) {
  if (index == 0 && apOn) {
    const char* title = !device::configured() ? "WI-FI SETUP"
                        : WiFi.status() != WL_CONNECTED ? "CAN'T FIND WI-FI" : "SETUP MODE";
    ui::drawSetup(s, title, apSsid, apPass);
  } else if (index == 0) ui::drawClaude(s, g_claude);
  else ui::drawPrinter(s, g_printer);
}

// Networks in priority order: the main one first, the backup only while the main one is not in range.
struct WifiNet {
  String ssid, pass;
};
static WifiNet nets[2];
static int netCount = 0;

// Index of the highest-priority network in range, -1 if none, -2 if the scan failed.
static int bestNetworkInRange() {
  int n = WiFi.scanNetworks();
  if (n < 0) return -2;
  int best = -1;
  for (int i = 0; i < n; ++i)
    for (int k = 0; k < netCount; ++k)
      if (WiFi.SSID(i) == nets[k].ssid && (best < 0 || k < best)) best = k;
  WiFi.scanDelete();
  return best;
}

static bool joinNetwork(int k, uint32_t timeoutMs) {
  Serial.printf("[wifi] connecting to %s\n", nets[k].ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(nets[k].ssid.c_str(), nets[k].pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("[wifi] %s, ip %s  ->  http://%s.local/\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), HOSTNAME);
  return true;
}

// Joins the best network in range, falling back down the list if joining fails.
static bool joinBest(uint32_t timeoutMs) {
  if (!netCount) return false;
  int best = bestNetworkInRange();
  if (best == -1) return false;
  for (int k = best < 0 ? 0 : best; k < netCount; ++k)
    if (joinNetwork(k, timeoutMs)) return true;
  return false;
}

static void connectWifi() {
  if (g_device.ssid.length()) nets[netCount++] = {g_device.ssid, g_device.pass};
  if (g_device.ssid2.length()) nets[netCount++] = {g_device.ssid2, g_device.pass2};
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  if (!device::configured()) return;
  for (int i = 0; i < 3 && !joinBest(10000); ++i) {
  }
  if (WiFi.status() != WL_CONNECTED) Serial.println("[wifi] not connected yet, will keep retrying");
}

// While offline, rescan every WIFI_RETRY_MS. While on a fallback network, look for the first
// network every WIFI_PREFER_MS and move back to it once it is in range.
static void maintainWifi() {
  static uint32_t lastTry = 0, lastPreferCheck = 0;
  if (apOn) {
    dns.processNextRequest();
    if (!device::configured()) return;
    if (WiFi.status() == WL_CONNECTED) {
      if (millis() - apSince >= SETUP_AP_MIN_MS && WiFi.softAPgetStationNum() == 0) stopSetupAp();
      return;
    }
    // Not while someone is on the hotspot: scanning and joining move it to another channel.
    if (WiFi.softAPgetStationNum() || millis() - lastTry < SETUP_RETRY_MS) return;
    lastTry = millis();
    joinBest(8000);
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastTry < WIFI_RETRY_MS) return;
    lastTry = millis();
    joinBest(8000);
    return;
  }
  if (netCount < 2 || WiFi.SSID() == nets[0].ssid || millis() - lastPreferCheck < WIFI_PREFER_MS) return;
  lastPreferCheck = millis();
  if (bestNetworkInRange() == 0 && !joinNetwork(0, 10000)) joinBest(8000);
}

static void logState() {
  Serial.printf("[claude] signed_in=%d valid=%d 5h=%.0f%% (%ds) week=%.0f%% (%ds) scoped=%d next=%us err='%s'\n",
                g_claude.loggedIn, g_claude.valid, g_claude.session.pct, g_claude.session.remaining(),
                g_claude.week.pct, g_claude.week.remaining(), g_claude.scopedCount, g_claude.nextPollInS,
                g_claude.error.c_str());
  if (printer)
    Serial.printf("[printer] online=%d %s %d%% %dmin layer %d/%d N%.0f B%.0f\n", g_printer.online,
                g_printer.gcodeState.c_str(), g_printer.percent, g_printer.remainingMin, g_printer.layer,
                g_printer.totalLayers, g_printer.nozzle, g_printer.bed);
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
  if (printer) panel1.setBrightness(applied);
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== desk-gauge ===");
  device::load();
  printer = device::printerOn();
  bool setupAsked = device::quickRestartsRequestSetup();
  settings::load();
  claude_auth::begin();
  claude_source::begin();

#if HAS_PANELS
  struct {
    RoundPanel* panel;
    LGFX_Sprite* frame;
  } screens[] = {{&panel0, &frame0}, {&panel1, &frame1}};
  for (auto& sc : screens) {
    if (sc.panel == &panel1 && !printer) break;
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
  if (!device::configured() || setupAsked || WiFi.status() != WL_CONNECTED) startSetupAp();
  // TLS and reset times need the clock
  configTzTime(g_device.timezone.c_str(), "pool.ntp.org", "time.google.com");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  if (printer) bambu_source::begin();
  web::begin(renderFace, printer ? 2 : 1, startSplash, startSaver);

  // Firmware updates over Wi-Fi (pio run -e esp32s3_ota -t upload), same password as the web page.
  if (g_device.webPass.length()) {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(g_device.webPass.c_str());
    ArduinoOTA.setMdnsEnabled(false);  // mDNS is already running above
    ArduinoOTA.onStart([] { Serial.println("[ota] update started"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] failed (%u)\n", e); });
    ArduinoOTA.begin();
  }
}

void loop() {
  static bool settled = false;
  if (!settled && millis() >= QUICK_BOOT_MS) {
    settled = true;
    device::bootSettled();
  }
  maintainWifi();
  ArduinoOTA.handle();
  if (printer) bambu_source::loop();
  claude_source::loop();
  web::loop();

#if HAS_PANELS
  applyBrightness();
  // Pointless with the backlight off at night, and it waits until there is a dashboard to wipe.
  if (g_settings.saverMin && !saverOn && !apOn && !splashRunning && g_claude.valid &&
      !(settings::isNight() && g_settings.nightBright == 0) &&
      millis() - saverDone >= g_settings.saverMin * 60000UL)
    startSaver();
  static uint32_t lastRender = 0;
  if (!splashRunning && !saverOn && millis() - lastRender >= (ui::animated(g_claude) ? RENDER_ANIM_MS : RENDER_MS)) {
    lastRender = millis();
    applyRotation();
    renderFace(0, frame0);
    pushFrame(panel0, frame0, prev0);
    if (printer) {
      renderFace(1, frame1);
      pushFrame(panel1, frame1, prev1);
    }
  }
#endif

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= SERIAL_LOG_MS) {
    lastLog = millis();
    logState();
  }
  delay(2);
}
