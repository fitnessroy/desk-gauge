#pragma once

// secrets.h is optional: its values are only the defaults for what the gauge keeps in flash
// (device setup, src/device.h). Without it (or with -DGAUGE_NO_SECRETS) the gauge starts its setup
// hotspot on first boot.
#if __has_include("secrets.h") && !defined(GAUGE_NO_SECRETS)
#include "secrets.h"
#endif

#define HOSTNAME "desk-gauge"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef WIFI_SSID2
#define WIFI_SSID2 ""
#endif
#ifndef WIFI_PASS2
#define WIFI_PASS2 ""
#endif

// Web page login. The user is fixed; the password is set on the setup hotspot (or WEB_PASS in secrets.h).
#ifndef WEB_USER
#define WEB_USER "admin"
#endif
#ifndef WEB_PASS
#define WEB_PASS ""
#endif
#define WEB_MAX_FAILS  5        // wrong logins in a row before the page locks
#define WEB_LOCKOUT_MS 60000

// Local time zone (POSIX TZ string), used for reset clock times. Default: Europe/Oslo.
#ifndef TIMEZONE
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

// Optional printer (second screen) and token bridge; empty = off.
#ifndef BAMBU_IP
#define BAMBU_IP ""
#endif
#ifndef BAMBU_SERIAL
#define BAMBU_SERIAL ""
#endif
#ifndef BAMBU_ACCESS_CODE
#define BAMBU_ACCESS_CODE ""
#endif
#ifndef CLAUDE_BRIDGE_URL
#define CLAUDE_BRIDGE_URL ""
#endif

// Setup hotspot: opens when the gauge has no Wi-Fi set up, can't join its network at boot, or after
// three quick restarts. Its name and password are on the screen (with a QR code to join).
#define SETUP_AP_PREFIX   "Desk-Gauge-"
#define SETUP_AP_MIN_MS   300000  // keep it up at least this long once opened
#define SETUP_RETRY_MS    60000   // while it is up, try the home network this often
#define QUICK_BOOT_MS     8000    // a boot shorter than this counts toward the three quick restarts

// Native panel resolution. The S3 build sets 360x360 for the ST77916 panels; the
// no-screen preview build keeps 240x240 to fit in heap. The UI scales from this.
#ifndef PANEL_W
#define PANEL_W 240
#endif
#ifndef PANEL_H
#define PANEL_H 240
#endif

// Claude usage polling. The interval is set on the web page (stored in flash); this is the default.
#define CLAUDE_POLL_DEFAULT_S 300
#define CLAUDE_POLL_MIN_S     120  // the usage endpoint answers 429 at 60 s
#define CLAUDE_POLL_MAX_S     3600
#define CLAUDE_BACKOFF_MAX_S  300   // on 429 / errors the interval doubles up to this
#define BRIDGE_POLL_MS        30000 // optional token bridge (bridge URL on the web page)

// Claude login. The device does its own OAuth login (PKCE, manual code paste) with Claude Code's
// public client, keeps its own refresh token in flash and refreshes it by itself.
// These are the values Claude Code uses; the usage endpoint is undocumented and may change.
#define CLAUDE_OAUTH_CLIENT_ID    "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
#define CLAUDE_OAUTH_AUTHORIZE    "https://claude.com/cai/oauth/authorize"
#define CLAUDE_OAUTH_TOKEN        "https://platform.claude.com/v1/oauth/token"
#define CLAUDE_OAUTH_REDIRECT     "https://platform.claude.com/oauth/code/callback"
#define CLAUDE_OAUTH_SCOPES       "user:profile user:inference"
#define CLAUDE_USAGE_URL          "https://api.anthropic.com/api/oauth/usage"
#define CLAUDE_OAUTH_BETA         "oauth-2025-04-20"

// Refresh / render
#define BAMBU_RECONNECT_MS  10000
#define BAMBU_STALE_MS      60000   // no report for this long -> treat as offline
#define RENDER_MS           500
#define RENDER_ANIM_MS      80      // faster frames while something animates (loading, mascot)
#define SPLASH_MS           10000   // boot splash length; setup carries on underneath it
#define SAVER_MS            9000    // screensaver length (interval is a web page setting)
#define FACE_ROTATE_MS      10000   // "rotate" face mode: switch between rings and bars
#define SERIAL_LOG_MS       10000
#define WIFI_RETRY_MS       20000   // rescan for a known network this often while offline
#define WIFI_PREFER_MS      180000  // on the backup network, check this often whether the main one is back

// Set to 1 to dump every raw printer MQTT report to serial (useful for new models like the P2S)
#define BAMBU_DUMP_RAW 0
#define BAMBU_MQTT_BUFFER 16384

// ESP32-S3 panel wiring (ST77916 QSPI). Each panel gets its own SPI host so they never contend.
// Panel 0 uses the left header, panel 1 (printer, when one is set up) the right header of the DevKitC-1.
// Avoid GPIO 35-37 (octal PSRAM), 19/20 (USB), 43/44 (UART0), 0/3/45/46 (strapping), 38/48 (RGB LED).
//                       SCLK IO0 IO1 IO2 IO3  CS RST  BL
#define PANEL0_PINS {    12,  11, 13, 14,  9, 10,  8, 18 }
#define PANEL1_PINS {    41,  40, 39,  2,  1, 42, 21, 47 }
