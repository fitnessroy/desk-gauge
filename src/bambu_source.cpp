#include "bambu_source.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "device.h"
#include "model.h"

namespace bambu_source {

static WiFiClientSecure tls;
static PubSubClient mqtt(tls);
static uint32_t lastAttempt = 0;
static String reportTopic, requestTopic;

// Reports after the initial "pushall" are deltas, so only overwrite fields that are present.
template <typename T>
static void take(JsonVariantConst v, T& out) {
  if (!v.isNull()) out = v.as<T>();
}

static void onMessage(char* topic, uint8_t* payload, unsigned int len) {
#if BAMBU_DUMP_RAW
  Serial.printf("[bambu] %u bytes: %.*s\n", len, (int)len, (const char*)payload);
#endif
  static JsonDocument filter;
  if (filter.isNull()) {
    JsonObject f = filter["print"].to<JsonObject>();
    for (const char* k : {"gcode_state", "subtask_name", "mc_percent", "mc_remaining_time",
                          "layer_num", "total_layer_num", "nozzle_temper", "nozzle_target_temper",
                          "bed_temper", "bed_target_temper", "chamber_temper", "print_error"}) {
      f[k] = true;
    }
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload, len, DeserializationOption::Filter(filter))) return;
  JsonObjectConst p = doc["print"];
  if (p.isNull()) return;

  PrinterState& s = g_printer;
  take(p["gcode_state"], s.gcodeState);
  take(p["subtask_name"], s.job);
  take(p["mc_percent"], s.percent);
  take(p["mc_remaining_time"], s.remainingMin);
  take(p["layer_num"], s.layer);
  take(p["total_layer_num"], s.totalLayers);
  take(p["nozzle_temper"], s.nozzle);
  take(p["nozzle_target_temper"], s.nozzleTarget);
  take(p["bed_temper"], s.bed);
  take(p["bed_target_temper"], s.bedTarget);
  take(p["chamber_temper"], s.chamber);
  take(p["print_error"], s.printError);
  s.online = true;
  s.lastReportMs = millis();
}

static void connect() {
  Serial.printf("[bambu] connecting to %s:8883\n", g_device.bambuIp.c_str());
  String clientId = String(HOSTNAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), "bblp", g_device.bambuCode.c_str())) {
    Serial.printf("[bambu] connect failed, state=%d\n", mqtt.state());
    return;
  }
  Serial.println("[bambu] connected");
  mqtt.subscribe(reportTopic.c_str());
  // Ask for a full status dump; afterwards the printer only sends changes.
  mqtt.publish(requestTopic.c_str(), "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
}

void begin() {
  reportTopic = "device/" + g_device.bambuSerial + "/report";
  requestTopic = "device/" + g_device.bambuSerial + "/request";
  tls.setInsecure();  // printer uses a self-signed certificate
  mqtt.setServer(g_device.bambuIp.c_str(), 8883);  // keeps the pointer; g_device outlives it
  mqtt.setBufferSize(BAMBU_MQTT_BUFFER);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
}

void loop() {
  if (g_printer.online && millis() - g_printer.lastReportMs > BAMBU_STALE_MS) {
    g_printer.online = false;
    g_printer.gcodeState = "OFFLINE";
  }
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (lastAttempt && millis() - lastAttempt < BAMBU_RECONNECT_MS) return;
  lastAttempt = millis();
  connect();
}

}  // namespace bambu_source
