#pragma once
#include <LovyanGFX.hpp>

#include "model.h"

// Gauge faces. Each draws a full frame into a square sprite of any size (laid out on a 240px grid
// and scaled). The Claude face follows g_settings: rings, bars, or both in rotation.
namespace ui {
int themeCount();
const char* themeName(int i);
void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c);
void drawPrinter(LGFX_Sprite& s, const PrinterState& p);
}
