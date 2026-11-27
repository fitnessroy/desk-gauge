#pragma once
#include <LovyanGFX.hpp>

#include "model.h"

// Gauge faces. Each draws a full frame into a square sprite of any size (laid out on a 240px grid
// and scaled). The Claude face follows g_settings: rings, bars, or both in rotation.
namespace ui {
int themeCount();
const char* themeName(int i);
void drawClaude(LGFX_Sprite& s, const ClaudeUsage& c);
// True while the Claude face shows something animated (loading, or the mascot on the bars face).
bool animated(const ClaudeUsage& c);
// Frame t (ms) of the boot splash, SPLASH_MS long.
void drawSplash(LGFX_Sprite& s, uint32_t t);
// Frame t (ms) of the screensaver (SAVER_MS long): the mascot wipes the screen with a sponge.
// `base` is a snapshot of the dashboard copied in first; nullptr = it is already in the sprite.
void drawSaver(LGFX_Sprite& s, const uint16_t* base, uint32_t t);
void drawPrinter(LGFX_Sprite& s, const PrinterState& p);
// Setup hotspot: a QR code that joins it, its name and password, and the page address.
void drawSetup(LGFX_Sprite& s, const String& title, const String& ssid, const String& pass);
}
