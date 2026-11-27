#pragma once
#include <Arduino.h>

// Device setup: networks, web password, time zone, printer and bridge. Set on the setup hotspot or
// the web page and kept in flash (NVS); values from secrets.h, if there is one, are the defaults.
// Changes take effect after a restart.
struct DeviceConfig {
  String ssid, pass;      // main network
  String ssid2, pass2;    // optional backup network
  String webPass;         // web page and OTA password; empty = not set up yet
  String timezone;        // POSIX TZ string
  String bambuIp, bambuSerial, bambuCode;
  String bridgeUrl;
};

extern DeviceConfig g_device;

namespace device {
void load();
void save();
// Wi-Fi and a web password are set, so the gauge can go online on its own.
bool configured();
bool printerOn();
// Clears everything in flash (device setup, settings, Claude login) and restarts with the setup
// hotspot; secrets.h defaults stay unused until the flash is erased with the programmer.
void factoryReset();
// Counts boots that end within a few seconds. Returns true when this is the third short boot in a
// row: unplugging the gauge during the intro twice and plugging it in again opens the setup hotspot.
bool quickRestartsRequestSetup();
void bootSettled();  // call once the gauge has been up long enough to clear the count
}
