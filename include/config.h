#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h - copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

#define HOSTNAME "desk-gauge"

// Native panel resolution. Common AliExpress round panels: GC9A01 240x240 (1.28"),
// ST77916 360x360 (1.8"/1.85", QSPI). The UI scales from this.
#ifndef PANEL_W
#define PANEL_W 240
#endif
#ifndef PANEL_H
#define PANEL_H 240
#endif

// Polling / refresh
#define CLAUDE_POLL_MS      60000
#define BAMBU_RECONNECT_MS  10000
#define BAMBU_STALE_MS      60000   // no report for this long -> treat as offline
#define RENDER_MS           500
#define SERIAL_LOG_MS       10000

// Set to 1 to dump every raw printer MQTT report to serial (useful for new models like the P2S)
#define BAMBU_DUMP_RAW 0
#define BAMBU_MQTT_BUFFER 16384

// ESP32-S3 panel wiring. Each panel gets its own SPI host so they never contend.
// Avoid GPIO 35-37 (octal PSRAM), 19/20 (USB), 0/3/45/46 (strapping), 48/38 (RGB LED).
#define P0_SCLK 12
#define P0_MOSI 11
#define P0_CS   10
#define P0_DC    9
#define P0_RST  14
#define P0_BL   13

#define P1_SCLK  4
#define P1_MOSI  5
#define P1_CS    6
#define P1_DC    7
#define P1_RST  15
#define P1_BL   16
