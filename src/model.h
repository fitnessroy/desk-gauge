#pragma once
#include <Arduino.h>
#include <math.h>
#include <time.h>

struct UsageWindow {
  bool present = false;
  float pct = 0;        // 0-100
  time_t resetsAt = 0;  // epoch seconds, 0 = unknown
  char label[16] = "";  // display name for model-scoped windows ("Opus", "Fable", ...)

  // Seconds until reset, -1 if unknown.
  int32_t remaining() const {
    time_t now = time(nullptr);
    if (!resetsAt || now < 1700000000) return -1;
    return resetsAt > now ? (int32_t)(resetsAt - now) : 0;
  }
};

// Fixed-step history (one value per STEP_S seconds), oldest first. Gaps are NAN.
template <int N, int STEP_S>
struct Series {
  float v[N];
  int count = 0, head = 0;  // head = next write position
  uint32_t lastSlot = 0;

  void add(time_t t, float x) {
    uint32_t slot = t / STEP_S;
    if (count && slot == lastSlot) {
      v[(head + N - 1) % N] = x;
      return;
    }
    if (count && slot > lastSlot) {
      for (uint32_t g = min<uint32_t>(slot - lastSlot - 1, N); g > 0; --g) push(NAN);
    }
    push(x);
    lastSlot = slot;
  }
  float at(int i) const { return v[(head - count + i + N) % N]; }  // 0 = oldest

 private:
  void push(float x) {
    v[head] = x;
    head = (head + 1) % N;
    if (count < N) ++count;
  }
};

static constexpr int kMaxScoped = 3;

struct ClaudeUsage {
  bool valid = false;
  bool loggedIn = false;
  UsageWindow session;               // 5-hour window
  UsageWindow week;                  // 7-day window, all models
  UsageWindow scoped[kMaxScoped];    // model-scoped weekly caps, when the plan reports them
  int scopedCount = 0;

  // Burn rate, from recent polls. -1 / NAN when there is not enough data.
  float sessionRatePerH = NAN;      // percentage points per hour
  int32_t sessionToLimitS = -1;     // seconds until 100% at the current rate
  float weekProjectedPct = NAN;     // where the week ends up at the current daily rate
  bool spike = false;               // >= 5 points in the last ~5 minutes

  Series<288, 300> sessionHist;     // 24h of 5-minute samples
  Series<168, 3600> weekHist;       // 7 days of hourly samples

  // Optional bridge (Claude Code transcripts on a computer)
  bool bridgeOk = false;
  uint64_t todayTokens = 0;
  uint32_t tokPerMin = 0;

  time_t fetchedAt = 0;             // last successful usage fetch (epoch)
  uint32_t nextPollInS = 0;
  String error;
};

struct PrinterState {
  bool online = false;
  String gcodeState = "OFFLINE";  // IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED
  String job;
  int percent = 0;
  int remainingMin = 0;
  int layer = 0;
  int totalLayers = 0;
  float nozzle = 0, nozzleTarget = 0;
  float bed = 0, bedTarget = 0;
  float chamber = 0;
  uint32_t printError = 0;
  uint32_t lastReportMs = 0;
};

extern ClaudeUsage g_claude;
extern PrinterState g_printer;

// Frame timing of the last screensaver run, in /state.json ("perf").
struct FramePerf {
  uint32_t frames = 0, renderMs = 0, pushMs = 0, totalMs = 0, gapMs = 0, lastEnd = 0;
};
extern FramePerf g_perf;
