// desk-gauge secrets: your own networks and passwords.
//
//   cp include/secrets.example.h include/secrets.h
//
// then fill in secrets.h. It is gitignored, so it never ends up on GitHub; this example file is the
// only one that is committed, and it holds no real values. A change here takes effect on the next
// flash (USB, or over Wi-Fi with the esp32s3_ota env).
#pragma once

// ---- Wi-Fi (required) ----------------------------------------------------------------------
// Main network. 2.4 GHz only: the ESP32-S3 can't see 5 GHz networks.
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Optional backup network, e.g. a phone hotspot. Used only while the main network is out of
// range; the gauge moves back to the main one within about 3 minutes once it returns.
// #define WIFI_SSID2 "other-ssid"
// #define WIFI_PASS2 "other-password"

// ---- Web page login (required) -------------------------------------------------------------
// The browser asks for this on every page of http://desk-gauge.local/ (or the gauge's IP).
// WEB_PASS is also the password for firmware updates over Wi-Fi (GAUGE_PASS='...').
#define WEB_USER "admin"
#define WEB_PASS "choose-a-password"

// ---- Claude ---------------------------------------------------------------------------------
// Nothing here: after flashing, open the gauge's web page and click "Open Claude sign-in".
// The login is stored on the device, not in this file.

// ---- Time zone (optional) -------------------------------------------------------------------
// POSIX TZ string for the clock and reset times. Default is Central Europe (Oslo, Berlin, Paris).
// #define TIMEZONE "GMT0BST,M3.5.0/1,M10.5.0"        // UK
// #define TIMEZONE "EST5EDT,M3.2.0,M11.1.0"          // US Eastern
// #define TIMEZONE "PST8PDT,M3.2.0,M11.1.0"          // US Pacific

// ---- Optional: Bambu Lab printer (LAN) on a second screen ----------------------------------
// Leave commented out for a Claude-only gauge. Find these on the printer screen:
//   Settings -> WLAN (IP), Settings -> LAN Only / Access Code, Settings -> Device (serial)
// #define BAMBU_IP          "192.168.1.50"
// #define BAMBU_SERIAL      "01P00A000000000"
// #define BAMBU_ACCESS_CODE "12345678"

// ---- Optional: token bridge ----------------------------------------------------------------
// bridge/claude_usage_bridge.py on a computer running Claude Code, for "tokens today" and the
// live tokens/min badge. Plan limits do not need it.
// #define CLAUDE_BRIDGE_URL "http://192.168.1.10:8787/claude.json"
