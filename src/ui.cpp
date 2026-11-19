#include "ui.h"

#include <WiFi.h>

#include "config.h"
#include "settings.h"

namespace ui {

// Arc gauges sweep 270 degrees with the gap at the bottom. LovyanGFX angles: 0 = 3 o'clock, clockwise.
static constexpr float kStart = 135, kSweep = 270;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

struct Theme {
  const char* name;
  uint16_t bg, track, text, dim, session, week, scoped, warn, bad, ok;
};

static const Theme kThemes[] = {
    {"Claude", rgb(12, 11, 10), rgb(42, 38, 35), rgb(240, 236, 230), rgb(140, 132, 124),
     rgb(217, 119, 87), rgb(106, 155, 204), rgb(176, 140, 220), rgb(240, 190, 60), rgb(235, 80, 70),
     rgb(110, 200, 120)},
    {"Terminal", rgb(0, 0, 0), rgb(0, 40, 8), rgb(130, 255, 130), rgb(40, 150, 50), rgb(0, 255, 80),
     rgb(0, 200, 70), rgb(0, 150, 60), rgb(220, 255, 0), rgb(255, 70, 60), rgb(0, 255, 80)},
    {"HUD", rgb(16, 10, 4), rgb(52, 36, 16), rgb(255, 205, 130), rgb(170, 120, 60), rgb(255, 170, 40),
     rgb(225, 140, 35), rgb(190, 110, 35), rgb(255, 225, 90), rgb(255, 80, 50), rgb(90, 230, 110)},
    {"Nord", rgb(46, 52, 64), rgb(67, 76, 94), rgb(236, 239, 244), rgb(150, 160, 180), rgb(136, 192, 208),
     rgb(129, 161, 193), rgb(180, 142, 173), rgb(235, 203, 139), rgb(191, 97, 106), rgb(163, 190, 140)},
    {"Dracula", rgb(40, 42, 54), rgb(68, 71, 90), rgb(248, 248, 242), rgb(150, 155, 190),
     rgb(255, 121, 198), rgb(189, 147, 249), rgb(139, 233, 253), rgb(241, 250, 140), rgb(255, 85, 85),
     rgb(80, 250, 123)},
    {"Mono", rgb(0, 0, 0), rgb(40, 40, 40), rgb(255, 255, 255), rgb(130, 130, 130), rgb(255, 255, 255),
     rgb(190, 190, 190), rgb(130, 130, 130), rgb(255, 200, 0), rgb(255, 60, 60), rgb(255, 255, 255)},
};
static constexpr int kThemeCount = sizeof kThemes / sizeof kThemes[0];

int themeCount() { return kThemeCount; }
const char* themeName(int i) { return kThemes[constrain(i, 0, kThemeCount - 1)].name; }

// Drawing context: layout is on a 240px grid, k scales it to the real sprite size.
struct Ctx {
  LGFX_Sprite& s;
  const Theme& t;
  float k;
  int c;  // centre (x = y)
  int X(float v) const { return (int)roundf(v * k); }
};

// Smooth DejaVu fonts at the size closest to (but not above) the requested 240-grid pixel height.
static const lgfx::IFont* font(float px) {
  static const struct {
    int px;
    const lgfx::IFont* f;
  } kFonts[] = {{72, &fonts::DejaVu72}, {56, &fonts::DejaVu56}, {40, &fonts::DejaVu40},
                {24, &fonts::DejaVu24}, {18, &fonts::DejaVu18}, {12, &fonts::DejaVu12}};
  for (const auto& f : kFonts)
    if (px >= f.px) return f.f;
  return &fonts::DejaVu9;
}

static void text(Ctx& g, const String& str, float x, float y, float size, uint16_t col,
                 textdatum_t datum = textdatum_t::middle_center) {
  g.s.setFont(font(size * g.k));
  g.s.setTextSize(1);
  g.s.setTextColor(col);
  g.s.setTextDatum(datum);
  g.s.drawString(str, g.X(x), g.X(y));
}

static void arc(Ctx& g, float r0, float r1, float from, float to, uint16_t col) {
  if (to <= from) return;
  int c = g.c, a = g.X(r0), b = g.X(r1);
  if (to > 360 && from < 360) {
    g.s.fillArc(c, c, a, b, from, 360, col);
    g.s.fillArc(c, c, a, b, 0, to - 360, col);
  } else if (from >= 360) {
    g.s.fillArc(c, c, a, b, from - 360, to - 360, col);
  } else {
    g.s.fillArc(c, c, a, b, from, to, col);
  }
}

static void polar(Ctx& g, float r, float deg, int& x, int& y) {
  float a = deg * DEG_TO_RAD;
  x = g.c + (int)roundf(cosf(a) * r * g.k);
  y = g.c + (int)roundf(sinf(a) * r * g.k);
}

// Ring gauge with rounded ends. `pace` (0-1, or <0 for none) marks how much of the window has
// elapsed, so being ahead of the tick means burning faster than an even pace.
static void ring(Ctx& g, float r0, float r1, float pct, uint16_t col, float pace) {
  pct = constrain(pct, 0.0f, 100.0f);
  float mid = (r0 + r1) / 2, cap = (r1 - r0) / 2 * g.k;
  arc(g, r0, r1, kStart, kStart + kSweep, g.t.track);
  int x, y;
  for (float a : {kStart, kStart + kSweep}) {  // rounded track ends
    polar(g, mid, a, x, y);
    g.s.fillCircle(x, y, cap, g.t.track);
  }
  if (pct > 0) {
    float end = kStart + kSweep * pct / 100.0f;
    arc(g, r0, r1, kStart, end, col);
    polar(g, mid, kStart, x, y);
    g.s.fillCircle(x, y, cap, col);
    polar(g, mid, end, x, y);
    g.s.fillCircle(x, y, cap, col);
  }
  if (pace >= 0 && pace <= 1) {
    int x0, y0, x1, y1;
    float a = kStart + kSweep * pace;
    polar(g, r0 - 2, a, x0, y0);
    polar(g, r1 + 2, a, x1, y1);
    for (int d = -1; d <= 1; ++d) g.s.drawLine(x0 + d, y0, x1 + d, y1, g.t.text);
  }
}

static float pace(const UsageWindow& w, float windowS) {
  int32_t left = w.remaining();
  return left < 0 ? -1 : 1.0f - left / windowS;
}

static uint16_t usageColor(const Theme& t, float pct, uint16_t normal) {
  return pct >= 90 ? t.bad : pct >= 70 ? t.warn : normal;
}

static String fmtDuration(int32_t secs) {
  if (secs < 0) return "--";
  int d = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60;
  char buf[16];
  if (d > 0) snprintf(buf, sizeof buf, "%dd %dh", d, h);
  else if (h > 0) snprintf(buf, sizeof buf, "%dh %02dm", h, m);
  else snprintf(buf, sizeof buf, "%dm", m);
  return buf;
}

static String fmtClock(time_t t) {
  if (!t) return "--:--";
  char buf[8];
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(buf, sizeof buf, "%H:%M", &tm);
  return buf;
}

static String fmtCount(uint64_t n) {
  char buf[16];
  if (n >= 1000000000ULL) snprintf(buf, sizeof buf, "%.1fB", n / 1e9);
  else if (n >= 1000000ULL) snprintf(buf, sizeof buf, "%.1fM", n / 1e6);
  else if (n >= 1000ULL) snprintf(buf, sizeof buf, "%.1fk", n / 1e3);
  else snprintf(buf, sizeof buf, "%u", (unsigned)n);
  return buf;
}

static String pctStr(float p) { return String((int)roundf(p)) + "%"; }

static String upper(String s) {
  s.toUpperCase();
  return s;
}

static void upTriangle(Ctx& g, float x, float y, float size, uint16_t col) {
  g.s.fillTriangle(g.X(x), g.X(y - size / 2), g.X(x - size / 2), g.X(y + size / 2), g.X(x + size / 2),
                   g.X(y + size / 2), col);
}

// Area sparkline of the last `points` samples, scaled to at least 0-10%.
template <class S>
static void sparkline(Ctx& g, const S& series, int points, float x, float y, float w, float h, uint16_t col) {
  int n = min(points, series.count);
  float hi = 10;
  for (int i = series.count - n; i < series.count; ++i)
    if (!isnan(series.at(i))) hi = max(hi, series.at(i));
  g.s.drawFastHLine(g.X(x), g.X(y + h), g.X(w), g.t.track);
  if (n < 2) return;
  int px = -1, py = -1;
  for (int i = 0; i < n; ++i) {
    float v = series.at(series.count - n + i);
    if (isnan(v)) {
      px = -1;
      continue;
    }
    int sx = g.X(x + w * (points - n + i) / (points - 1));
    int sy = g.X(y + h - h * v / hi);
    g.s.drawFastVLine(sx, sy, g.X(y + h) - sy, g.t.track);
    if (px >= 0) {
      g.s.drawLine(px, py, sx, sy, col);
      g.s.drawLine(px, py + 1, sx, sy + 1, col);
    }
    px = sx;
    py = sy;
  }
}

// Centred text with an optional warning triangle in front of it.
static void alertText(Ctx& g, const String& str, float y, uint16_t col, bool icon) {
  g.s.setFont(font(12 * g.k));
  float w = g.s.textWidth(str) / g.k + (icon ? 11 : 0);
  float x = 120 - w / 2;
  if (icon) upTriangle(g, x + 4, y, 7, col);
  text(g, str, x + (icon ? 11 : 0), y, 12, col, textdatum_t::middle_left);
}

// One-line status under the numbers: the most useful warning, else something informative.
static void statusLine(Ctx& g, const ClaudeUsage& c, float y) {
  int32_t left = c.session.remaining();
  if (c.sessionToLimitS >= 0 && left >= 0 && c.sessionToLimitS < left) {
    alertText(g, "limit in " + fmtDuration(c.sessionToLimitS), y, g.t.bad, true);
  } else if (c.spike) {
    alertText(g, "SPIKE", y, g.t.warn, true);
  } else if (!isnan(c.weekProjectedPct) && c.weekProjectedPct >= 100) {
    alertText(g, "week on pace for " + pctStr(min(c.weekProjectedPct, 999.0f)), y, g.t.warn, false);
  } else if (c.bridgeOk && c.todayTokens) {
    alertText(g, fmtCount(c.todayTokens) + " tok today", y, g.t.dim, false);
  } else if (c.error.length()) {
    alertText(g, "stale", y, g.t.warn, false);
  }
}

static void liveBadge(Ctx& g, const ClaudeUsage& c, float y) {
  String s = "LIVE " + fmtCount(c.tokPerMin) + "/min";
  float w = s.length() * 6.8f;
  g.s.fillCircle(g.X(120 - w / 2 - 4), g.X(y), g.X(3), g.t.ok);
  text(g, s, 124, y, 12, g.t.ok);
}

// Connected network name with a small signal icon, or "no Wi-Fi" in the warning colour.
static void wifiLine(Ctx& g, float y) {
  bool up = WiFi.status() == WL_CONNECTED;
  String name = up ? WiFi.SSID() : String("no Wi-Fi");
  if (name.length() > 16) name = name.substring(0, 15) + "..";
  uint16_t col = up ? g.t.dim : g.t.warn;
  g.s.setFont(font(12 * g.k));
  float x = 120 - (g.s.textWidth(name) / g.k + 12) / 2;
  int cx = g.X(x + 4), cy = g.X(y + 4);
  g.s.fillCircle(cx, cy, max(1, g.X(1)), col);
  g.s.fillArc(cx, cy, g.X(3.5), g.X(4.5), 225, 315, col);
  g.s.fillArc(cx, cy, g.X(6.5), g.X(7.5), 225, 315, col);
  text(g, name, x + 12, y, 12, col, textdatum_t::middle_left);
}

// Current local time, once NTP has set the clock.
static void clockText(Ctx& g, float y, float size) {
  time_t now = time(nullptr);
  if (now < 1700000000) return;
  text(g, fmtClock(now), 120, y, size, g.t.text);
}

// Not signed in / no data yet: empty rings and instructions.
static bool drawStatus(Ctx& g, const ClaudeUsage& c) {
  if (c.valid) return false;
  ring(g, 104, 116, 0, 0, -1);
  ring(g, 92, 100, 0, 0, -1);
  text(g, "CLAUDE", 120, 70, 12, g.t.session);
  if (!c.loggedIn) {
    text(g, "sign in at", 120, 106, 12, g.t.dim);
    text(g, HOSTNAME ".local", 120, 126, 18, g.t.text);
    // Some computers can't resolve .local names; the IP always works.
    if (WiFi.status() == WL_CONNECTED) text(g, "or " + WiFi.localIP().toString(), 120, 148, 12, g.t.dim);
  } else {
    // A bright arc circles the outer ring and three dots pulse in turn under the word.
    uint32_t ms = millis();
    float a = (ms % 1500) * 360.0f / 1500;
    arc(g, 104, 116, a, a + 70, g.t.session);
    text(g, "loading", 120, 108, 18, g.t.text);
    int lit = (ms / 300) % 3;
    for (int i = 0; i < 3; ++i)
      g.s.fillCircle(g.X(110 + i * 10), g.X(128), g.X(i == lit ? 3 : 2), i == lit ? g.t.session : g.t.track);
  }
  if (c.error.length() && c.loggedIn) text(g, c.error.substring(0, 24), 120, 160, 12, g.t.bad);
  wifiLine(g, 184);
  return true;
}

static void drawRings(Ctx& g, const ClaudeUsage& c) {
  const Theme& t = g.t;
  const UsageWindow* sc = c.scopedCount ? &c.scoped[0] : nullptr;

  ring(g, 104, 116, c.session.pct, usageColor(t, c.session.pct, t.session), pace(c.session, 5 * 3600));
  ring(g, 92, 100, c.week.pct, usageColor(t, c.week.pct, t.week), pace(c.week, 7 * 86400));
  if (sc) ring(g, 83, 88, sc->pct, usageColor(t, sc->pct, t.scoped), -1);

  if (c.tokPerMin > 0) liveBadge(g, c, 58);
  else wifiLine(g, 52);
  text(g, "SESSION", 120, 74, 12, t.dim);
  text(g, pctStr(c.session.pct), 120, 104, 48, usageColor(t, c.session.pct, t.text));
  text(g, "resets " + fmtDuration(c.session.remaining()), 120, 134, 12, t.dim);
  text(g, "WEEK " + pctStr(c.week.pct) + "  " + fmtDuration(c.week.remaining()), 120, 154, 12,
       usageColor(t, c.week.pct, t.week));
  if (sc) text(g, upper(sc->label) + " " + pctStr(sc->pct), 120, 170, 12, usageColor(t, sc->pct, t.scoped));
  // Below the inner ring ends the bottom gap is wider, so the alert line and 5h sparkline sit there.
  statusLine(g, c, sc ? 194 : 182);
  sparkline(g, c.sessionHist, 60, 88, sc ? 206 : 200, 64, 14, usageColor(t, c.session.pct, t.session));
  clockText(g, 222, 12);
}

static void barRow(Ctx& g, const char* label, const UsageWindow& w, float windowS, uint16_t col, float y) {
  const Theme& t = g.t;
  col = usageColor(t, w.pct, col);
  text(g, label, 40, y, 12, t.text, textdatum_t::middle_left);
  text(g, fmtDuration(w.remaining()), 160, y, 12, t.dim, textdatum_t::middle_right);
  text(g, pctStr(w.pct), 200, y, 12, col, textdatum_t::middle_right);
  float x = 40, bw = 160, by = y + 9, bh = 6;
  g.s.fillRoundRect(g.X(x), g.X(by), g.X(bw), g.X(bh), g.X(bh / 2), t.track);
  float fill = bw * constrain(w.pct, 0.0f, 100.0f) / 100.0f;
  if (fill > 0) g.s.fillRoundRect(g.X(x), g.X(by), max(g.X(fill), g.X(bh)), g.X(bh), g.X(bh / 2), col);
  float p = pace(w, windowS);
  if (p >= 0 && windowS > 0) g.s.fillRect(g.X(x + bw * p) - 1, g.X(by - 2), 3, g.X(bh + 4), t.text);
}

static void drawBars(Ctx& g, const ClaudeUsage& c) {
  const Theme& t = g.t;
  text(g, "CLAUDE", 120, 28, 12, t.session);
  if (c.tokPerMin > 0) liveBadge(g, c, 42);
  else wifiLine(g, 42);

  float y = 70;
  barRow(g, "Session", c.session, 5 * 3600, t.session, y);
  barRow(g, "Week", c.week, 7 * 86400, t.week, y += 30);
  for (int i = 0; i < c.scopedCount && i < 2; ++i) barRow(g, c.scoped[i].label, c.scoped[i], 7 * 86400, t.scoped, y += 30);

  float sy = y + 26;
  text(g, "24h", 40, sy + 8, 12, t.dim, textdatum_t::middle_left);
  sparkline(g, c.sessionHist, 288, 66, sy, 134, 18, t.session);

  statusLine(g, c, sy + 34);
  clockText(g, 190, 18);
  text(g, "reset " + fmtClock(c.session.resetsAt), 120, 214, 12, t.dim);
}

void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c) {
  Ctx g{s, kThemes[constrain(g_settings.theme, 0, kThemeCount - 1)], s.width() / 240.0f, s.width() / 2};
  s.fillScreen(g.t.bg);
  if (drawStatus(g, c)) return;

  bool bars = g_settings.face == FaceMode::Bars ||
              (g_settings.face == FaceMode::Rotate && (millis() / FACE_ROTATE_MS) % 2);
  if (bars) drawBars(g, c);
  else drawRings(g, c);
}

static uint16_t stateColor(const Theme& t, const String& st) {
  if (st == "RUNNING" || st == "PREPARE") return t.ok;
  if (st == "PAUSE") return t.warn;
  if (st == "FAILED") return t.bad;
  if (st == "FINISH") return t.week;
  return t.dim;
}

void drawPrinter(LGFX_Sprite& s, const PrinterState& p) {
  Ctx g{s, kThemes[constrain(g_settings.theme, 0, kThemeCount - 1)], s.width() / 240.0f, s.width() / 2};
  const Theme& t = g.t;
  s.fillScreen(t.bg);

  uint16_t col = p.printError ? t.bad : stateColor(t, p.gcodeState);
  bool active = p.gcodeState == "RUNNING" || p.gcodeState == "PAUSE" || p.gcodeState == "PREPARE";
  ring(g, 104, 116, active || p.gcodeState == "FINISH" ? p.percent : 0, col, -1);

  text(g, p.printError ? String("ERROR") : p.gcodeState, 120, 60, 12, col);

  if (!p.online) {
    text(g, "printer", 120, 110, 18, t.dim);
    text(g, "offline", 120, 134, 18, t.dim);
    return;
  }

  if (active) {
    text(g, String(p.percent) + "%", 120, 100, 48, t.text);
    text(g, fmtDuration(p.remainingMin * 60) + " left", 120, 134, 12, t.text);
    text(g, "layer " + String(p.layer) + "/" + String(p.totalLayers), 120, 152, 12, t.dim);
  } else {
    text(g, p.gcodeState == "FINISH" ? "done" : "idle", 120, 110, 24, t.text);
  }

  char temps[40];
  snprintf(temps, sizeof temps, "N %.0f/%.0f  B %.0f/%.0f", p.nozzle, p.nozzleTarget, p.bed, p.bedTarget);
  text(g, temps, 120, 174, 12, t.warn);
  if (p.job.length()) text(g, p.job.substring(0, 22), 120, 194, 12, t.dim);
}

}  // namespace ui
