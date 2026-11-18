#pragma once
#include <Arduino.h>

// User settings, edited on the web page and kept in flash (NVS).
enum class FaceMode : uint8_t { Rings = 0, Bars = 1, Rotate = 2 };

struct Settings {
  uint16_t pollS = 60;        // Claude usage poll interval
  uint8_t theme = 0;          // index into ui::kThemes
  FaceMode face = FaceMode::Rotate;
  uint8_t brightness = 200;   // backlight 0-255
};

extern Settings g_settings;

namespace settings {
void load();
void save();
}
