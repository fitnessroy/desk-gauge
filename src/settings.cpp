#include "settings.h"

#include <Preferences.h>

#include "config.h"

Settings g_settings;

namespace settings {

void load() {
  Preferences p;
  p.begin("settings", true);
  g_settings.pollS = constrain(p.getUShort("poll", CLAUDE_POLL_DEFAULT_S), CLAUDE_POLL_MIN_S, CLAUDE_POLL_MAX_S);
  g_settings.theme = p.getUChar("theme", 0);
  g_settings.face = (FaceMode)constrain(p.getUChar("face", (uint8_t)FaceMode::Rotate), 0, 2);
  g_settings.brightness = p.getUChar("bright", 200);
  g_settings.nightOn = p.getBool("nOn", false);
  g_settings.nightStart = p.getUShort("nStart", 23 * 60) % 1440;
  g_settings.nightEnd = p.getUShort("nEnd", 7 * 60) % 1440;
  g_settings.nightBright = p.getUChar("nBright", 0);
  p.end();
}

void save() {
  Preferences p;
  p.begin("settings", false);
  p.putUShort("poll", g_settings.pollS);
  p.putUChar("theme", g_settings.theme);
  p.putUChar("face", (uint8_t)g_settings.face);
  p.putUChar("bright", g_settings.brightness);
  p.putBool("nOn", g_settings.nightOn);
  p.putUShort("nStart", g_settings.nightStart);
  p.putUShort("nEnd", g_settings.nightEnd);
  p.putUChar("nBright", g_settings.nightBright);
  p.end();
}

bool isNight() {
  time_t now = time(nullptr);
  if (!g_settings.nightOn || now < 1700000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min, a = g_settings.nightStart, b = g_settings.nightEnd;
  return a < b ? (m >= a && m < b) : a > b ? (m >= a || m < b) : false;
}

}  // namespace settings
