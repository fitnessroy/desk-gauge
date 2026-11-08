#include "claude_source.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "model.h"

namespace claude_source {

static uint32_t lastPoll = 0;
static bool firstPoll = true;

static void readWindow(JsonVariantConst v, UsageWindow& w) {
  w.present = !v.isNull();
  if (!w.present) return;
  w.pct = v["pct"] | 0.0f;
  w.resetsInS = v["resets_in"] | -1;
}

static void poll() {
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(CLAUDE_BRIDGE_URL)) {
    g_claude.error = "bad bridge url";
    return;
  }
  int code = http.GET();
  if (code != 200) {
    g_claude.error = code < 0 ? http.errorToString(code) : "HTTP " + String(code);
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    g_claude.error = String("json: ") + err.c_str();
    return;
  }

  g_claude.fetchedAtMs = millis();
  readWindow(doc["session"], g_claude.session);
  readWindow(doc["week"], g_claude.week);
  readWindow(doc["week_opus"], g_claude.weekOpus);
  g_claude.todayTokens = doc["today_tokens"] | (uint64_t)0;
  g_claude.todayOutputTokens = doc["today_output_tokens"] | (uint64_t)0;
  g_claude.error = doc["error"] | "";
  g_claude.valid = g_claude.session.present || g_claude.todayTokens > 0;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!firstPoll && millis() - lastPoll < CLAUDE_POLL_MS) return;
  firstPoll = false;
  lastPoll = millis();
  poll();
}

}  // namespace claude_source
