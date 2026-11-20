#pragma once
#include <Arduino.h>

// User settings, edited on the web page and kept in flash (NVS).
enum class FaceMode : uint8_t { Rings = 0, Bars = 1, Rotate = 2 };

struct Settings {
  uint16_t pollS = 300;       // Claude usage poll interval (load() applies CLAUDE_POLL_DEFAULT_S)
  uint8_t theme = 0;          // index into ui::kThemes
  FaceMode face = FaceMode::Rotate;
  uint8_t brightness = 200;   // backlight 0-255
  // Night mode: between nightStart and nightEnd (minutes after local midnight, may wrap past
  // midnight) the backlight drops to nightBright; 0 switches it off.
  bool nightOn = false;
  uint16_t nightStart = 23 * 60;
  uint16_t nightEnd = 7 * 60;
  uint8_t nightBright = 0;
};

extern Settings g_settings;

namespace settings {
void load();
void save();
// True when night mode is on and the local clock (once set) is inside the night window.
bool isNight();
}
