#pragma once
#include <Arduino.h>

struct UsageWindow {
  bool present = false;
  float pct = 0;            // 0-100
  int32_t resetsInS = -1;   // seconds until reset at fetch time, -1 = unknown
};

struct ClaudeUsage {
  bool valid = false;
  UsageWindow session;      // 5-hour window
  UsageWindow week;         // 7-day window
  UsageWindow weekOpus;     // 7-day Opus window (if plan has one)
  uint64_t todayTokens = 0;
  uint64_t todayOutputTokens = 0;
  uint32_t fetchedAtMs = 0;
  String error;

  // Seconds left on a window, adjusted for time since fetch.
  int32_t remaining(const UsageWindow& w) const {
    if (w.resetsInS < 0) return -1;
    int32_t left = w.resetsInS - (int32_t)((millis() - fetchedAtMs) / 1000);
    return left < 0 ? 0 : left;
  }
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
