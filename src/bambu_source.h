#pragma once

// Connects to a Bambu Lab printer's local MQTT broker (TLS :8883) and updates g_printer.
namespace bambu_source {
void begin();
void loop();
}
