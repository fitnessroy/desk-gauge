#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "bambu_source.h"
#include "claude_source.h"
#include "config.h"
#include "model.h"
#include "ui.h"
#include "web.h"

#if HAS_PANELS
#include "panels.h"
static RoundPanel panel0(SPI2_HOST, P0_SCLK, P0_MOSI, P0_DC, P0_CS, P0_RST, P0_BL, 6);
static RoundPanel panel1(SPI3_HOST, P1_SCLK, P1_MOSI, P1_DC, P1_CS, P1_RST, P1_BL, 7);
static LGFX_Sprite frame0(&panel0), frame1(&panel1);
#endif

ClaudeUsage g_claude;
PrinterState g_printer;

static void renderFace(int index, LGFX_Sprite& s) {
  if (index == 0) ui::drawClaude(s, g_claude);
  else ui::drawPrinter(s, g_printer);
}

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] ip %s  ->  http://%s.local/\n", WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    Serial.println("[wifi] not connected yet, will keep retrying");
  }
}

static void logState() {
  Serial.printf("[claude] valid=%d 5h=%.0f%% (%ds) week=%.0f%% today=%llu tok err='%s'\n",
                g_claude.valid, g_claude.session.pct, g_claude.remaining(g_claude.session),
                g_claude.week.pct, g_claude.todayTokens, g_claude.error.c_str());
  Serial.printf("[printer] online=%d %s %d%% %dmin layer %d/%d N%.0f B%.0f heap=%u\n",
                g_printer.online, g_printer.gcodeState.c_str(), g_printer.percent,
                g_printer.remainingMin, g_printer.layer, g_printer.totalLayers, g_printer.nozzle,
                g_printer.bed, ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== desk-gauge ===");

#if HAS_PANELS
  for (auto* p : {&panel0, &panel1}) {
    p->init();
    p->setBrightness(200);
    p->fillScreen(TFT_BLACK);
  }
  for (auto* f : {&frame0, &frame1}) {
    f->setColorDepth(16);
    f->setPsram(true);
    f->createSprite(PANEL_W, PANEL_H);
  }
#endif

  connectWifi();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  bambu_source::begin();
  web::begin(renderFace);
}

void loop() {
  bambu_source::loop();
  claude_source::loop();
  web::loop();

#if HAS_PANELS
  static uint32_t lastRender = 0;
  if (millis() - lastRender >= RENDER_MS) {
    lastRender = millis();
    renderFace(0, frame0);
    frame0.pushSprite(0, 0);
    renderFace(1, frame1);
    frame1.pushSprite(0, 0);
  }
#endif

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= SERIAL_LOG_MS) {
    lastLog = millis();
    logState();
  }
  delay(2);
}
