#include "ui.h"

namespace ui {

// Arc gauges sweep 270 degrees with the gap at the bottom. LovyanGFX angles: 0 = 3 o'clock, clockwise.
static constexpr float kStart = 135, kSweep = 270;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t BG = rgb(8, 8, 12);
static constexpr uint16_t TRACK = rgb(38, 38, 48);
static constexpr uint16_t TEXT = rgb(235, 235, 240);
static constexpr uint16_t DIM = rgb(130, 130, 145);
static constexpr uint16_t CLAUDE = rgb(217, 119, 87);  // Claude orange
static constexpr uint16_t OK = rgb(80, 200, 120);
static constexpr uint16_t WARN = rgb(240, 190, 60);
static constexpr uint16_t BAD = rgb(235, 80, 70);
static constexpr uint16_t BLUE = rgb(80, 160, 240);

static void arc(LGFX_Sprite& s, int r0, int r1, float from, float to, uint16_t col) {
  int cx = s.width() / 2, cy = s.height() / 2;
  if (to <= from) return;
  if (to > 360 && from < 360) {
    s.fillArc(cx, cy, r0, r1, from, 360, col);
    s.fillArc(cx, cy, r0, r1, 0, to - 360, col);
  } else if (from >= 360) {
    s.fillArc(cx, cy, r0, r1, from - 360, to - 360, col);
  } else {
    s.fillArc(cx, cy, r0, r1, from, to, col);
  }
}

// Track + filled portion for a 0-100 value.
static void gauge(LGFX_Sprite& s, int r0, int r1, float pct, uint16_t col) {
  pct = constrain(pct, 0.0f, 100.0f);
  arc(s, r0, r1, kStart, kStart + kSweep, TRACK);
  arc(s, r0, r1, kStart, kStart + kSweep * pct / 100.0f, col);
}

static uint16_t usageColor(float pct) { return pct >= 90 ? BAD : pct >= 70 ? WARN : CLAUDE; }

static String fmtDuration(int32_t secs) {
  if (secs < 0) return "--";
  int d = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60;
  char buf[16];
  if (d > 0) snprintf(buf, sizeof buf, "%dd %dh", d, h);
  else if (h > 0) snprintf(buf, sizeof buf, "%dh %02dm", h, m);
  else snprintf(buf, sizeof buf, "%dm", m);
  return buf;
}

static String fmtTokens(uint64_t n) {
  char buf[16];
  if (n >= 1000000000ULL) snprintf(buf, sizeof buf, "%.1fB", n / 1e9);
  else if (n >= 1000000ULL) snprintf(buf, sizeof buf, "%.1fM", n / 1e6);
  else if (n >= 1000ULL) snprintf(buf, sizeof buf, "%.1fk", n / 1e3);
  else snprintf(buf, sizeof buf, "%u", (unsigned)n);
  return buf;
}

static void text(LGFX_Sprite& s, const String& str, float y, const lgfx::IFont* font, uint16_t col,
                 float scale) {
  s.setFont(font);
  s.setTextSize(scale);
  s.setTextColor(col);
  s.setTextDatum(textdatum_t::middle_center);
  s.drawString(str, s.width() / 2, (int)y);
}

void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c) {
  const float k = s.width() / 240.0f;  // scale factor
  const int R = s.width() / 2;
  s.fillScreen(BG);

  // Outer ring: 5-hour session. Inner ring: 7-day.
  gauge(s, R - 18 * k, R - 4 * k, c.session.pct, usageColor(c.session.pct));
  gauge(s, R - 30 * k, R - 23 * k, c.week.pct, c.week.pct >= 90 ? BAD : BLUE);

  text(s, "CLAUDE", 62 * k, &fonts::FreeSansBold9pt7b, CLAUDE, k);

  if (!c.valid) {
    text(s, "no data", 120 * k, &fonts::FreeSansBold12pt7b, DIM, k);
    if (c.error.length()) text(s, c.error.substring(0, 22), 150 * k, &fonts::Font2, BAD, k);
    return;
  }

  text(s, String((int)roundf(c.session.pct)) + "%", 108 * k, &fonts::FreeSansBold24pt7b, TEXT, k);
  text(s, "5h  resets " + fmtDuration(c.remaining(c.session)), 140 * k, &fonts::Font2, DIM, k);
  text(s, "week " + String((int)roundf(c.week.pct)) + "%  " + fmtDuration(c.remaining(c.week)),
       158 * k, &fonts::Font2, BLUE, k);
  text(s, fmtTokens(c.todayTokens) + " tok today", 182 * k, &fonts::Font2, TEXT, k);
  if (c.error.length()) text(s, "stale", 202 * k, &fonts::Font2, WARN, k);
}

static uint16_t stateColor(const String& st) {
  if (st == "RUNNING" || st == "PREPARE") return OK;
  if (st == "PAUSE") return WARN;
  if (st == "FAILED") return BAD;
  if (st == "FINISH") return BLUE;
  return DIM;
}

void drawPrinter(LGFX_Sprite& s, const PrinterState& p) {
  const float k = s.width() / 240.0f;
  const int R = s.width() / 2;
  s.fillScreen(BG);

  uint16_t col = p.printError ? BAD : stateColor(p.gcodeState);
  bool active = p.gcodeState == "RUNNING" || p.gcodeState == "PAUSE" || p.gcodeState == "PREPARE";
  gauge(s, R - 18 * k, R - 4 * k, active || p.gcodeState == "FINISH" ? p.percent : 0, col);

  text(s, p.printError ? String("ERROR") : p.gcodeState, 60 * k, &fonts::FreeSansBold9pt7b, col, k);

  if (!p.online) {
    text(s, "printer", 112 * k, &fonts::FreeSansBold12pt7b, DIM, k);
    text(s, "offline", 138 * k, &fonts::FreeSansBold12pt7b, DIM, k);
    return;
  }

  if (active) {
    text(s, String(p.percent) + "%", 102 * k, &fonts::FreeSansBold24pt7b, TEXT, k);
    text(s, fmtDuration(p.remainingMin * 60) + " left", 134 * k, &fonts::Font2, TEXT, k);
    text(s, "layer " + String(p.layer) + "/" + String(p.totalLayers), 152 * k, &fonts::Font2, DIM, k);
  } else {
    text(s, p.gcodeState == "FINISH" ? "done" : "idle", 110 * k, &fonts::FreeSansBold18pt7b, TEXT, k);
  }

  char temps[40];
  snprintf(temps, sizeof temps, "N %.0f/%.0f  B %.0f/%.0f", p.nozzle, p.nozzleTarget, p.bed, p.bedTarget);
  text(s, temps, 174 * k, &fonts::Font2, WARN, k);
  if (p.job.length()) text(s, p.job.substring(0, 22), 194 * k, &fonts::Font2, DIM, k);
}

}  // namespace ui
