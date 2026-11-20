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
  } else if (c.fetchedAt && time(nullptr) > 1700000000) {
    // How old the numbers are: dim normally, amber once two polls in a row have been missed.
    int32_t age = max<int32_t>(0, time(nullptr) - c.fetchedAt);
    bool old = age > 2 * (int32_t)g_settings.pollS;
    alertText(g, age < 60 ? String("synced just now") : "synced " + fmtDuration(age) + " ago", y,
              old ? g.t.warn : g.t.dim, false);
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

// The Claude Code mascot, pixel for pixel from its terminal banner (quadrant blocks, so each
// pixel is twice as tall as it is wide). '#' body, 'o' eye, 'a'/'b' the two pairs of legs.
// It walks on the spot (the leg pairs take turns) and blinks every few seconds.
static const char* const kMascot[] = {
    "   ############   ",
    "   ##o######o##   ",
    " ################ ",
    "   ############   ",
    "    a b    b a    ",
};

// u is the pixel width on the 240 grid; the figure is 18u wide and 10u tall, centred on cx, cy.
// legs: 0 or 1 = which pair is down while walking, 2 = standing on all four.
static void mascotFrame(Ctx& g, float cx, float cy, float u, uint16_t col, int legs, bool blink) {
  int pw = max(1, g.X(u)), ph = pw * 2;
  int x0 = g.X(cx) - 9 * pw, y0 = g.X(cy) - 5 * pw;
  for (int r = 0; r < 5; ++r)
    for (int c = 0; kMascot[r][c]; ++c) {
      char p = kMascot[r][c];
      int x = x0 + c * pw, y = y0 + r * ph;
      if (p == '#' || (p == 'o' && blink)) g.s.fillRect(x, y, pw, ph, col);
      else if (p == 'a' || p == 'b') g.s.fillRect(x, y, pw, legs == 2 || (p == 'a') == (legs == 1) ? ph : pw, col);
    }
}

static void mascot(Ctx& g, float cx, float cy, float u, uint16_t col) {
  uint32_t ms = millis();
  mascotFrame(g, cx, cy, u, col, (ms / 300) % 2, ms % 4000 < 150);
}

// Not signed in / no data yet: empty rings and instructions.
static bool drawStatus(Ctx& g, const ClaudeUsage& c) {
  if (c.valid) return false;
  ring(g, 104, 116, 0, 0, -1);
  ring(g, 92, 100, 0, 0, -1);
  mascot(g, 120, 48, 2, g.t.session);
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
  mascot(g, 120, 14, 1.4f, t.session);
  text(g, "CLAUDE", 120, 30, 12, t.session);
  if (c.tokPerMin > 0) liveBadge(g, c, 44);
  else wifiLine(g, 44);

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

// ---- Boot splash -------------------------------------------------------------------------------
// A fixed 10 s intro in Claude's own palette, independent of the theme. Everything is a function
// of t (ms since boot), so the splash task can draw any frame at any rate.

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float phase(uint32_t t, float a, float b) { return clamp01((t - a) / (b - a)); }
static float easeOut(float x) { return 1 - powf(1 - x, 3); }
static float easeInOut(float x) { return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2; }
static float easeOutBack(float x) {
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2);
}

// a..b in RGB565 by f (0 = a, 1 = b).
static uint16_t blend(uint16_t a, uint16_t b, float f) {
  f = clamp01(f);
  auto mix = [f](int x, int y) { return (int)(x + (y - x) * f + 0.5f); };
  int r = mix(a >> 11, b >> 11), gg = mix((a >> 5) & 63, (b >> 5) & 63), bl = mix(a & 31, b & 31);
  return (r << 11) | (gg << 5) | bl;
}

// Text with extra space between letters (tracking on the 240 grid), centred on x.
static void trackedText(Ctx& g, const char* str, float x, float y, float size, float tracking, uint16_t col) {
  g.s.setFont(font(size * g.k));
  g.s.setTextSize(1);
  g.s.setTextColor(col);
  g.s.setTextDatum(textdatum_t::middle_left);
  int n = strlen(str), total = 0;
  char ch[2] = {0, 0};
  for (int i = 0; i < n; ++i) ch[0] = str[i], total += g.s.textWidth(ch);
  float px = g.X(x) - (total + tracking * g.k * (n - 1)) / 2;
  for (int i = 0; i < n; ++i) {
    ch[0] = str[i];
    g.s.drawString(ch, (int)roundf(px), g.X(y));
    px += g.s.textWidth(ch) + tracking * g.k;
  }
}

void drawSplash(LGFX_Sprite& s, uint32_t t) {
  Ctx g{s, kThemes[0], s.width() / 240.0f, s.width() / 2};
  const uint16_t bg = rgb(20, 20, 19), orange = rgb(217, 119, 87), cream = rgb(250, 249, 245),
                 grey = rgb(150, 146, 138), track = rgb(46, 44, 41);
  float out = 1 - easeInOut(phase(t, SPLASH_MS - 700, SPLASH_MS));  // final fade to black
  auto col = [&](uint16_t c, float a) { return blend(bg, c, a * out); };
  s.fillScreen(bg);

  // Progress ring around the edge: track fades in, then the orange arc sweeps once from the top.
  float tr = phase(t, 100, 800);
  if (tr > 0) arc(g, 113, 115.5f, 0, 360, col(track, tr));
  float p = easeInOut(phase(t, 500, SPLASH_MS - 900));
  if (p > 0) {
    arc(g, 113, 115.5f, 270, 270 + 360 * p, col(orange, 1));
    int x, y;
    polar(g, 114.25f, 270 + 360 * p, x, y);
    s.fillSmoothCircle(x, y, g.X(2.6f), col(cream, 1));
  }

  // The Claude star: twelve tapered rays grow out one after another, then it breathes; at the end
  // it spins half a turn with a small pulse.
  static const float kLen[12] = {1.0f, 0.74f, 0.9f, 0.68f, 0.97f, 0.78f, 0.88f, 0.7f, 1.0f, 0.8f, 0.86f, 0.72f};
  float sec = t / 1000.0f;
  float settle = easeOut(phase(t, 150, 1700));
  float fin = phase(t, 8300, 9300);
  float rot = (-70 * (1 - settle) + sec * 6 + 180 * easeInOut(fin)) * DEG_TO_RAD;
  float R = 25 * g.k * (1 + 0.14f * sinf(PI * fin));
  int sx = g.X(120), sy = g.X(70);
  for (int i = 0; i < 12; ++i) {
    float grow = phase(t, 150 + i * 55, 700 + i * 55);
    if (grow <= 0) continue;
    float e = easeOutBack(grow);
    float len = R * kLen[i] * e * (1 + 0.035f * sinf(sec * 2.2f + i * 1.1f));
    float a = rot + i * (PI / 6);
    s.drawWedgeLine(sx, sy, sx + (int)roundf(cosf(a) * len), sy + (int)roundf(sinf(a) * len),
                    R * 0.14f * min(e, 1.0f), 1.0f, col(orange, 1));
  }
  if (t > 150) s.fillSmoothCircle(sx, sy, (int)roundf(R * 0.17f), col(orange, 1));

  // "Claude", letter by letter, each sliding up a little as it fades in.
  {
    const char* word = "Claude";
    g.s.setFont(font(27 * g.k));
    g.s.setTextSize(1);
    g.s.setTextDatum(textdatum_t::middle_left);
    int total = g.s.textWidth(word), px = g.X(120) - total / 2;
    char ch[2] = {0, 0};
    for (int i = 0; word[i]; ++i) {
      ch[0] = word[i];
      float a = easeOut(phase(t, 1500 + i * 90, 2000 + i * 90));
      if (a > 0) {
        g.s.setTextColor(col(cream, a));
        g.s.drawString(ch, px, g.X(113 + 5 * (1 - a)));
      }
      px += g.s.textWidth(ch);
    }
  }

  // "GAUGE": orange capitals whose spacing opens up as they fade in.
  float ga = easeOut(phase(t, 2200, 3000));
  if (ga > 0) trackedText(g, "GAUGE", 120, 139, 12, 2 + 5 * ga, col(orange, ga));

  // Divider growing from the centre, then the byline.
  float dv = easeOut(phase(t, 2700, 3300));
  if (dv > 0) s.fillRect(g.X(120 - 20 * dv), g.X(152), max(1, g.X(40 * dv)), max(1, g.X(0.7f)), col(blend(bg, orange, 0.55f), 1));
  float by = easeOut(phase(t, 3100, 3700));
  if (by > 0) text(g, "by FitnessProductions", 120, 164, 8, col(grey, by));

  // The mascot walks in from the left, stops in the middle, blinks twice and hops at the finale.
  float walkIn = phase(t, 3800, 6200);
  if (walkIn > 0) {
    bool walking = walkIn < 1;
    float mx = 70 + (120 - 70) * easeOut(walkIn);
    float hop = sinf(PI * phase(t, 8450, 9050)) * 9;
    float bob = walking && (t / 140) % 2 ? 0.8f : 0;
    bool blink = (t > 6600 && t < 6750) || (t > 7800 && t < 7950);
    mascotFrame(g, mx, 191 - bob - hop, 1.9f, col(orange, easeOut(phase(t, 3800, 4300))),
                walking ? (t / 140) % 2 : 2, blink);
  }

  // Real network state under it, so the wait means something.
  float st = easeOut(phase(t, 4400, 5000));
  if (st > 0) {
    String msg;
    if (WiFi.status() == WL_CONNECTED) msg = "connected to " + WiFi.SSID();
    else {
      msg = "joining Wi-Fi";
      for (int i = 0; i < (int)(t / 350) % 4; ++i) msg += '.';
    }
    text(g, msg, 120, 213, 8, col(grey, st));
  }
}

// ---- Screensaver -------------------------------------------------------------------------------
// The mascot wipes the screen with a kitchen sponge: drops in at the top, scrubs six zigzag passes
// (the dashboard behind the sponge goes dark and soap bubbles drift up), admires the clean screen
// while it sparkles, then a glint sweeps the dashboard back in. Also a pure function of t.

static float hash01(int n) {
  float x = sinf(n * 12.9898f) * 43758.5453f;
  return x - floorf(x);
}

static void sponge(Ctx& g, float cx, float cy, uint16_t bg) {
  const uint16_t body = rgb(250, 214, 64), pore = rgb(214, 170, 34), pad = rgb(58, 150, 82);
  const float w = 20, h = 26;
  int x = g.X(cx - w / 2), y = g.X(cy - h / 2), W = g.X(w), H = g.X(h), r = g.X(3.5f);
  g.s.fillRoundRect(x, y, W, H, r, body);
  g.s.fillRoundRect(x, y, W, g.X(7), r, pad);  // green scouring side on top
  g.s.fillRect(x, y + g.X(4), W, g.X(3), pad);
  static const float kPores[][2] = {{-5, 1}, {4, 3}, {-2, 7}, {6, 9}, {-6, 10}, {1, 11}};
  for (const auto& p : kPores) g.s.fillCircle(g.X(cx + p[0]), g.X(cy + p[1]), max(1, g.X(1.3f)), pore);
  (void)bg;
}

static void sparkle(Ctx& g, float cx, float cy, float r, uint16_t col) {
  if (r < 0.5f) return;
  int x = g.X(cx), y = g.X(cy), R = g.X(r);
  g.s.drawWedgeLine(x - R, y, x + R, y, 0.6f, 0.6f, col);
  g.s.drawWedgeLine(x, y - R, x, y + R, 0.6f, 0.6f, col);
  g.s.fillSmoothCircle(x, y, max(1, R / 4), col);
}

void drawSaver(LGFX_Sprite& s, const uint16_t* base, uint32_t t) {
  if (base) memcpy(s.getBuffer(), base, s.bufferLength());
  Ctx g{s, kThemes[constrain(g_settings.theme, 0, kThemeCount - 1)], s.width() / 240.0f, s.width() / 2};
  const uint16_t bg = g.t.bg, accent = g.t.session, foam = rgb(185, 220, 245);
  const int kBands = 6;
  const float kBandH = 240.0f / kBands, mu = 1.6f, reach = 9 * mu + 8;  // mascot half-width + arm to sponge
  const uint32_t kDrop = 700, kPass = 850, kMove = 220, kStep = kPass + kMove;
  const uint32_t wipeEnd = kDrop + (kBands - 1) * kStep + kPass, cleanEnd = wipeEnd + 1100,
                 revealEnd = cleanEnd + 900;
  auto bandY = [&](int k) { return kBandH * (k + 0.5f); };
  auto half = [&](int k) { float d = bandY(k) - 120; return sqrtf(max(0.0f, 120 * 120 - d * d)) - 16; };
  auto passX = [&](int k, float f) {  // sponge x along pass k; even passes go left to right
    float a = 120 - half(k), b = 120 + half(k);
    return k % 2 ? b + (a - b) * f : a + (b - a) * f;
  };
  float sec = t / 1000.0f;

  if (t < wipeEnd) {
    int k = 0;
    float f = 0, y = bandY(0), drop = 0;
    if (t < kDrop) {
      drop = 1 - easeOutBack(phase(t, 0, kDrop));  // falls in from above with a little bounce
    } else {
      k = min<int>((t - kDrop) / kStep, kBands - 1);
      uint32_t local = t - kDrop - k * kStep;
      f = min(1.0f, local / (float)kPass);
      if (local > kPass && k < kBands - 1) y += kBandH * easeInOut((local - kPass) / (float)kMove);
      y = local > kPass ? y : bandY(k);
    }
    // Wiped so far: every band above, and this band up to the sponge.
    if (k > 0) s.fillRect(0, 0, s.width(), g.X(kBandH * k), bg);
    float x = passX(k, f);
    int bandTop = g.X(kBandH * k), bandH = g.X(kBandH) + 1;
    if (t >= kDrop) {
      if (k % 2 == 0) s.fillRect(0, bandTop, g.X(x), bandH, bg);
      else s.fillRect(g.X(x), bandTop, s.width() - g.X(x), bandH, bg);
    }
    // Soap bubbles left along the passes: they swell, drift up and pop.
    for (int b = 0; b < kBands * 4; ++b) {
      int pk = b / 4;
      float bf = ((b % 4) + 0.3f + 0.4f * hash01(b)) / 4;
      float born = kDrop + pk * kStep + bf * kPass, age = t - born;
      if (age < 0 || age > 1400) continue;
      float r = (2.5f + 3.5f * hash01(b + 50)) * easeOut(min(1.0f, age / 300));
      float bx = passX(pk, bf) + (hash01(b + 90) - 0.5f) * 10, by = bandY(pk) + (hash01(b + 130) - 0.5f) * 22 - age / 140;
      s.drawCircle(g.X(bx), g.X(by), g.X(r), foam);
      s.fillCircle(g.X(bx - r * 0.35f), g.X(by - r * 0.35f), max(1, g.X(r * 0.18f)), rgb(255, 255, 255));
    }
    // The cleaner: sponge in front on the leading side, scrubbing up and down.
    int dir = k % 2 ? -1 : 1;
    float scrub = t >= kDrop && f < 1 ? sinf(sec * 2 * PI * 6) * 6 : 0;
    float my = y - drop * 60;
    mascotFrame(g, x - dir * reach, my + 4, mu, accent, t >= kDrop && f < 1 ? (t / 120) % 2 : 2, false);
    sponge(g, x, my + scrub, bg);
    return;
  }

  if (t < cleanEnd) {
    // Spotless: sparkles pop in turn while the mascot hops to the middle and admires its work.
    s.fillScreen(bg);
    static const float kSpark[][3] = {{78, 70, 0}, {162, 92, 180}, {112, 128, 360}, {150, 168, 520}};
    for (const auto& p : kSpark) {
      float a = phase(t, wipeEnd + p[2], wipeEnd + p[2] + 500);
      sparkle(g, p[0], p[1], 9 * sinf(PI * a), rgb(255, 255, 255));
    }
    float hopIn = easeInOut(phase(t, wipeEnd, wipeEnd + 450));
    float x0 = passX(kBands - 1, 1) + reach;
    float mx = x0 + (120 - x0) * hopIn, my = bandY(kBands - 1) + (196 - bandY(kBands - 1)) * hopIn;
    my -= sinf(PI * hopIn) * 18 + sinf(PI * phase(t, wipeEnd + 650, wipeEnd + 950)) * 8;
    bool blink = t > wipeEnd + 500 && t < wipeEnd + 620;
    mascotFrame(g, mx, my, mu, accent, 2, blink);
    return;
  }

  // A glint sweeps left to right and the dashboard is back behind it; the mascot walks off ahead.
  float gx = -10 + 260 * easeInOut(phase(t, cleanEnd, revealEnd));
  if (gx < 240) s.fillRect(max(0, g.X(gx)), 0, s.width() - max(0, g.X(gx)), s.height(), bg);
  s.fillRect(g.X(gx) - 1, 0, 3, s.height(), rgb(255, 255, 255));
  s.fillRect(g.X(gx) - 4, 0, 3, s.height(), blend(bg, accent, 0.5f));
  mascotFrame(g, gx + 22, 196, mu, accent, (t / 120) % 2, false);
}

static bool barsShown() {
  return g_settings.face == FaceMode::Bars ||
         (g_settings.face == FaceMode::Rotate && (millis() / FACE_ROTATE_MS) % 2);
}

bool animated(const ClaudeUsage& c) { return !c.valid || barsShown(); }

void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c) {
  Ctx g{s, kThemes[constrain(g_settings.theme, 0, kThemeCount - 1)], s.width() / 240.0f, s.width() / 2};
  s.fillScreen(g.t.bg);
  if (drawStatus(g, c)) return;
  if (barsShown()) drawBars(g, c);
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
