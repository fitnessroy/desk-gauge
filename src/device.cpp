#include "device.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "config.h"

DeviceConfig g_device;

namespace device {

void load() {
  Preferences p;
  p.begin("device", true);
  // A key that was never saved falls back to secrets.h; one saved empty stays empty.
  auto get = [&](const char* key, const char* def) { return p.isKey(key) ? p.getString(key) : String(def); };
  g_device.ssid = get("ssid", WIFI_SSID);
  g_device.pass = get("pass", WIFI_PASS);
  g_device.ssid2 = get("ssid2", WIFI_SSID2);
  g_device.pass2 = get("pass2", WIFI_PASS2);
  g_device.webPass = get("webPass", WEB_PASS);
  g_device.timezone = get("tz", TIMEZONE);
  g_device.bambuIp = get("bIp", BAMBU_IP);
  g_device.bambuSerial = get("bSerial", BAMBU_SERIAL);
  g_device.bambuCode = get("bCode", BAMBU_ACCESS_CODE);
  g_device.bridgeUrl = get("bridge", CLAUDE_BRIDGE_URL);
  p.end();
  if (!g_device.timezone.length()) g_device.timezone = TIMEZONE;
}

void save() {
  Preferences p;
  p.begin("device", false);
  p.putString("ssid", g_device.ssid);
  p.putString("pass", g_device.pass);
  p.putString("ssid2", g_device.ssid2);
  p.putString("pass2", g_device.pass2);
  p.putString("webPass", g_device.webPass);
  p.putString("tz", g_device.timezone);
  p.putString("bIp", g_device.bambuIp);
  p.putString("bSerial", g_device.bambuSerial);
  p.putString("bCode", g_device.bambuCode);
  p.putString("bridge", g_device.bridgeUrl);
  p.end();
}

bool configured() { return g_device.ssid.length() && g_device.webPass.length(); }

bool printerOn() { return g_device.bambuIp.length() && g_device.bambuSerial.length() && g_device.bambuCode.length(); }

void factoryReset() {
  Serial.println("[device] factory reset");
  nvs_flash_erase();
  delay(100);
  ESP.restart();
}

bool quickRestartsRequestSetup() {
  Preferences p;
  p.begin("boot", false);
  uint8_t n = p.getUChar("quick", 0) + 1;
  p.putUChar("quick", n >= 3 ? 0 : n);
  p.end();
  return n >= 3;
}

void bootSettled() {
  Preferences p;
  p.begin("boot", false);
  p.putUChar("quick", 0);
  p.end();
}

}  // namespace device
