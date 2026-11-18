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
  p.end();
}

void save() {
  Preferences p;
  p.begin("settings", false);
  p.putUShort("poll", g_settings.pollS);
  p.putUChar("theme", g_settings.theme);
  p.putUChar("face", (uint8_t)g_settings.face);
  p.putUChar("bright", g_settings.brightness);
  p.end();
}

}  // namespace settings
