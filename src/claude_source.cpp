#include "claude_source.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

#include "claude_auth.h"
#include "config.h"
#include "model.h"
#include "net.h"
#include "settings.h"

namespace claude_source {

static uint32_t nextPollMs = 0;
static uint32_t backoffS = 0;  // 0 = normal interval

// Recent session polls for burn rate / spike detection. Cleared when the window resets.
struct Sample {
  time_t t;
  float pct;
};
static Sample recent[64];
static int recentCount = 0;

void pollNow() { nextPollMs = millis(); }

// The last good reading is kept in flash, so a restart shows it at once instead of "loading"
// and waits out the poll interval before fetching again (restarts in a row otherwise hit 429).
struct Cached {
  uint8_t version;
  UsageWindow session, week, scoped[kMaxScoped];
  int8_t scopedCount;
  time_t fetchedAt;
};
static constexpr uint8_t kCacheVersion = 1;
static bool restored = false;

static void saveCache(const ClaudeUsage& c) {
  Cached k{kCacheVersion, c.session, c.week, {}, (int8_t)c.scopedCount, c.fetchedAt};
  for (int i = 0; i < c.scopedCount; ++i) k.scoped[i] = c.scoped[i];
  Preferences p;
  p.begin("usage", false);
  p.putBytes("last", &k, sizeof k);
  p.end();
}

void begin() {
  Cached k;
  Preferences p;
  p.begin("usage", true);
  size_t n = p.getBytes("last", &k, sizeof k);
  p.end();
  if (n != sizeof k || k.version != kCacheVersion || !claude_auth::loggedIn()) return;
  ClaudeUsage& c = g_claude;
  c.session = k.session;
  c.week = k.week;
  c.scopedCount = constrain(k.scopedCount, 0, kMaxScoped);
  for (int i = 0; i < c.scopedCount; ++i) c.scoped[i] = k.scoped[i];
  c.fetchedAt = k.fetchedAt;
  c.valid = c.session.present || c.week.present;
  restored = c.valid;
}

void forget() {
  Preferences p;
  p.begin("usage", false);
  p.clear();
  p.end();
  g_claude.valid = false;
}

static int daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// "2026-04-14T10:00:00.123+00:00" / "...Z" -> epoch seconds. Also accepts a plain number.
static time_t parseTime(JsonVariantConst v) {
  if (v.is<long>() || v.is<double>()) return (time_t)v.as<double>();
  const char* s = v | "";
  int Y, M, D, h, m;
  double sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
  long off = 0;
  const char* z = strpbrk(strchr(s, 'T') + 1, "+-Z");
  if (z && *z != 'Z') {
    int oh = 0, om = 0;
    sscanf(z + 1, "%d:%d", &oh, &om);
    off = (oh * 3600L + om * 60L) * (*z == '-' ? -1 : 1);
  }
  return (time_t)daysFromCivil(Y, M, D) * 86400 + h * 3600 + m * 60 + (long)sec - off;
}

static void readWindow(JsonVariantConst v, UsageWindow& w, const char* pctKey = "utilization") {
  w.present = !v.isNull() && !v[pctKey].isNull();
  if (!w.present) return;
  w.pct = v[pctKey].as<float>();
  w.resetsAt = parseTime(v["resets_at"]);
}

static void addScoped(ClaudeUsage& c, const UsageWindow& w, const char* label) {
  if (!w.present || c.scopedCount >= kMaxScoped) return;
  UsageWindow& dst = c.scoped[c.scopedCount++];
  dst = w;
  strlcpy(dst.label, label, sizeof dst.label);
}

static void updateTrends(ClaudeUsage& c) {
  time_t now = time(nullptr);
  const float p = c.session.pct;

  // A drop means the 5-hour window rolled over; old samples no longer apply.
  if (recentCount && p + 2 < recent[recentCount - 1].pct) recentCount = 0;
  if (recentCount == 64) {
    memmove(recent, recent + 1, sizeof(Sample) * 63);
    --recentCount;
  }
  recent[recentCount++] = {now, p};

  // Rate over (up to) the last hour, needs at least 10 minutes of samples.
  c.sessionRatePerH = NAN;
  c.sessionToLimitS = -1;
  c.spike = false;
  for (int i = 0; i < recentCount; ++i) {
    time_t dt = now - recent[i].t;
    if (dt > 3600) continue;
    if (dt >= 600) {
      c.sessionRatePerH = (p - recent[i].pct) * 3600.0f / dt;
      if (c.sessionRatePerH > 0.1f) c.sessionToLimitS = (int32_t)((100 - p) / c.sessionRatePerH * 3600);
    }
    break;
  }
  for (int i = recentCount - 1; i >= 0; --i) {
    time_t dt = now - recent[i].t;
    if (dt < 180) continue;
    if (dt <= 480 && p - recent[i].pct >= 5) c.spike = true;
    break;
  }

  c.sessionHist.add(now, p);
  c.weekHist.add(now, c.week.pct);

  // Week projection from the daily rate over (up to) the last 24h, at least 6h of history.
  c.weekProjectedPct = NAN;
  int n = c.weekHist.count;
  for (int back = min(n - 1, 24); back >= 6; --back) {
    float old = c.weekHist.at(n - 1 - back);
    if (isnan(old) || old > c.week.pct) continue;
    int32_t left = c.week.remaining();
    if (left < 0) break;
    float perS = (c.week.pct - old) / (back * 3600.0f);
    c.weekProjectedPct = c.week.pct + perS * left;
    break;
  }
}

// Returns false and sets g_claude.error on failure. `retryAfterS` is set on 429.
static bool fetchUsage(uint32_t& retryAfterS) {
  String err;
  for (int attempt = 0; attempt < 2; ++attempt) {
    String token = claude_auth::accessToken(err);
    if (!token.length()) {
      g_claude.error = err;
      return false;
    }
    net::Response r = net::request("GET", CLAUDE_USAGE_URL, "",
                                   {{"Authorization", "Bearer " + token},
                                    {"anthropic-beta", CLAUDE_OAUTH_BETA},
                                    {"Content-Type", "application/json"},
                                    {"User-Agent", "desk-gauge/1.0"}});
    if (r.status == 401 && attempt == 0) {
      claude_auth::invalidateAccess();  // expired early or revoked: refresh and retry once
      continue;
    }
    if (r.status == 429) retryAfterS = r.retryAfter.toInt();
    if (r.status != 200) {
      g_claude.error = "usage " + r.error();
      return false;
    }

    JsonDocument doc;
    if (DeserializationError e = deserializeJson(doc, r.body)) {
      g_claude.error = String("usage json: ") + e.c_str();
      return false;
    }

    ClaudeUsage& c = g_claude;
    readWindow(doc["five_hour"], c.session);
    readWindow(doc["seven_day"], c.week);
    c.scopedCount = 0;
    // Newer responses list model caps in limits[]; older ones have seven_day_<model> fields.
    for (JsonVariantConst row : doc["limits"].as<JsonArrayConst>()) {
      if (row["kind"] != "weekly_scoped") continue;
      UsageWindow w;
      readWindow(row, w, row["percent"].isNull() ? "utilization" : "percent");
      addScoped(c, w, row["scope"]["model"]["display_name"] | "Model");
    }
    if (!c.scopedCount) {
      UsageWindow w;
      readWindow(doc["seven_day_opus"], w);
      addScoped(c, w, "Opus");
      readWindow(doc["seven_day_sonnet"], w);
      addScoped(c, w, "Sonnet");
    }
    c.valid = c.session.present || c.week.present;
    c.fetchedAt = time(nullptr);
    c.error = "";
    if (c.valid) {
      updateTrends(c);
      saveCache(c);
    }
    return true;
  }
  return false;
}

#ifdef CLAUDE_BRIDGE_URL
static void pollBridge() {
  static uint32_t last = 0;
  static bool first = true;
  if (!first && millis() - last < BRIDGE_POLL_MS) return;
  first = false;
  last = millis();

  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(CLAUDE_BRIDGE_URL) || http.GET() != 200) {
    g_claude.bridgeOk = false;
    http.end();
    return;
  }
  JsonDocument filter;
  filter["today_tokens"] = true;
  filter["tok_per_min"] = true;
  JsonDocument doc;
  bool ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  g_claude.bridgeOk = ok;
  if (!ok) return;
  g_claude.todayTokens = doc["today_tokens"] | (uint64_t)0;
  g_claude.tokPerMin = doc["tok_per_min"] | 0u;
}
#endif

void loop() {
  g_claude.loggedIn = claude_auth::loggedIn();
  int32_t wait = (int32_t)(nextPollMs - millis());
  g_claude.nextPollInS = wait > 0 ? wait / 1000 : 0;
  if (WiFi.status() != WL_CONNECTED) return;
#ifdef CLAUDE_BRIDGE_URL
  pollBridge();
#endif
  if (!net::clockSet()) return;
  if (restored) {
    // First poll after a restart: wait until the cached reading is one poll interval old.
    restored = false;
    int64_t age = (int64_t)time(nullptr) - g_claude.fetchedAt;
    if (age >= 0 && age < g_settings.pollS) {
      nextPollMs = millis() + (g_settings.pollS - age) * 1000;
      return;
    }
  }
  if ((int32_t)(nextPollMs - millis()) > 0) return;

  uint32_t retryAfterS = 0;
  uint32_t intervalS = g_settings.pollS;
  if (!g_claude.loggedIn) {
    g_claude.error = "not signed in";
    intervalS = 5;  // just re-check the flag; no network
  } else if (fetchUsage(retryAfterS)) {
    backoffS = 0;
  } else {
    backoffS = min<uint32_t>(backoffS ? backoffS * 2 : g_settings.pollS * 2, CLAUDE_BACKOFF_MAX_S);
    intervalS = max<uint32_t>(backoffS, retryAfterS);
    Serial.printf("[claude] %s, retry in %us\n", g_claude.error.c_str(), intervalS);
  }
  nextPollMs = millis() + intervalS * 1000;
}

}  // namespace claude_source
