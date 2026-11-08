#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h - copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

#define HOSTNAME "desk-gauge"

// Native panel resolution. The S3 build sets 360x360 for the ST77916 panels; the
// no-screen preview build keeps 240x240 to fit in heap. The UI scales from this.
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

// ESP32-S3 panel wiring (ST77916 QSPI). Each panel gets its own SPI host so they never contend.
// Panel 0 uses the left header, panel 1 the right header of the DevKitC-1.
// Avoid GPIO 35-37 (octal PSRAM), 19/20 (USB), 43/44 (UART0), 0/3/45/46 (strapping), 38/48 (RGB LED).
//                       SCLK IO0 IO1 IO2 IO3  CS RST  BL
#define PANEL0_PINS {    12,  11, 13, 14,  9, 10,  8, 18 }
#define PANEL1_PINS {    41,  40, 39,  2,  1, 42, 21, 47 }
