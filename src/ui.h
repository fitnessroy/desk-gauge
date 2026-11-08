#pragma once
#include <LovyanGFX.hpp>

#include "model.h"

// Gauge faces. Both draw a full frame into a square sprite of any size (scaled from 240px).
namespace ui {
void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c);
void drawPrinter(LGFX_Sprite& s, const PrinterState& p);
}
