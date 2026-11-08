// Copy to include/secrets.h and fill in. secrets.h is gitignored.
#pragma once

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// Bambu Lab printer (LAN). Find these on the printer screen:
//   Settings -> WLAN (IP), Settings -> LAN Only / Access Code, Settings -> Device (serial)
#define BAMBU_IP          "192.168.1.50"
#define BAMBU_SERIAL      "01P00A000000000"
#define BAMBU_ACCESS_CODE "12345678"

// URL of the Claude usage bridge (bridge/claude_usage_bridge.py)
#define CLAUDE_BRIDGE_URL "http://192.168.1.10:8787/claude.json"
