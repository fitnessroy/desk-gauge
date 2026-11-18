// Copy to include/secrets.h and fill in. secrets.h is gitignored.
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Optional second network, used when the first one is out of range.
// #define WIFI_SSID2 "other-ssid"
// #define WIFI_PASS2 "other-password"

// Login for the gauge's web page (browser asks for it). Required.
#define WEB_USER "admin"
#define WEB_PASS "choose-a-password"

// Claude needs nothing here: sign in once at http://desk-gauge.local/ after flashing.

// Optional: Bambu Lab printer (LAN) on a second screen. Leave commented out for a Claude-only gauge.
// Find these on the printer screen:
//   Settings -> WLAN (IP), Settings -> LAN Only / Access Code, Settings -> Device (serial)
// #define BAMBU_IP          "192.168.1.50"
// #define BAMBU_SERIAL      "01P00A000000000"
// #define BAMBU_ACCESS_CODE "12345678"

// Optional: bridge/claude_usage_bridge.py on a machine running Claude Code, for "tokens today"
// and the live tokens/min badge. Plan limits do not need it.
// #define CLAUDE_BRIDGE_URL "http://192.168.1.10:8787/claude.json"
