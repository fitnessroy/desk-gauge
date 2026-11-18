#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

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
static LGFX_Sprite frame0(&panel0);
#if ENABLE_PRINTER
static RoundPanel panel1(SPI3_HOST, PanelPins PANEL1_PINS, 7);
static LGFX_Sprite frame1(&panel1);
#endif
#endif

ClaudeUsage g_claude;
PrinterState g_printer;

static void renderFace(int index, LGFX_Sprite& s) {
  if (index == 0) ui::drawClaude(s, g_claude);
  else ui::drawPrinter(s, g_printer);
}

// Joins whichever configured network is in range (strongest first); WIFI_SSID2 is optional.
static WiFiMulti wifiMulti;

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
#ifdef WIFI_SSID2
  wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);
  Serial.printf("[wifi] connecting to %s or %s\n", WIFI_SSID, WIFI_SSID2);
#else
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
#endif
  for (int i = 0; i < 3 && wifiMulti.run(8000) != WL_CONNECTED; ++i) {
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] %s, ip %s  ->  http://%s.local/\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    Serial.println("[wifi] not connected yet, will keep retrying");
  }
}

// While disconnected, rescan now and then so the gauge can fall back to the other network.
static void maintainWifi() {
  static uint32_t lastTry = 0;
  if (WiFi.status() == WL_CONNECTED || millis() - lastTry < WIFI_RETRY_MS) return;
  lastTry = millis();
  if (wifiMulti.run(5000) == WL_CONNECTED) {
    Serial.printf("[wifi] reconnected to %s, ip %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  }
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
    sc.panel->init();
    sc.panel->fillScreen(TFT_BLACK);
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
  if (millis() - lastRender >= RENDER_MS) {
    lastRender = millis();
    applyBrightness();
    renderFace(0, frame0);
    frame0.pushSprite(0, 0);
#if ENABLE_PRINTER
    renderFace(1, frame1);
    frame1.pushSprite(0, 0);
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
