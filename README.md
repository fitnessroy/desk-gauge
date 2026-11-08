# desk-gauge

A two-screen round desk gauge on an ESP32:

- **Screen 0 – Claude:** the outer ring is your 5-hour session limit, the inner ring is your weekly limit. It also shows reset countdowns and today's Claude Code token count.
- **Screen 1 – Bambu Lab printer (P2S, also X1/P1/A1):** state, progress ring, time left, layers, nozzle/bed temps and job name.

```
 Bambu printer ──MQTT/TLS :8883──┐
                                 ├──> ESP32 ──> 2× round SPI panels
 claude_usage_bridge.py ─HTTP────┘        └──> http://desk-gauge.local/ (live preview)
 (Mac / k3s, reads Claude Code login)
```

## Hardware

| | Board | Screens |
|---|---|---|
| `esp32dev` | ESP32 DevKit V1 (WROOM-32) | none: use the web preview |
| `esp32s3` | ESP32-S3 DevKitC-1 **N16R8** | 2× round panel (GC9A01 SPI by default) |

### Wiring (ESP32-S3)

Each panel has its own SPI bus. You can change the pins in `include/config.h`.

| Panel pin | Panel 0 (Claude) | Panel 1 (Printer) |
|---|---|---|
| VCC | 3V3 | 3V3 |
| GND | GND | GND |
| SCL / SCK | GPIO 12 | GPIO 4 |
| SDA / MOSI | GPIO 11 | GPIO 5 |
| CS | GPIO 10 | GPIO 6 |
| DC | GPIO 9 | GPIO 7 |
| RST | GPIO 14 | GPIO 15 |
| BL | GPIO 13 | GPIO 16 |

> If your panel isn't a GC9A01 (a 1.8" round panel is often 360×360 ST77916 over QSPI), you only need to change `src/panels.h` and `PANEL_W/PANEL_H`. The UI scales automatically.

## Setup

1. **Install PlatformIO:** `brew install platformio`
2. **Secrets:** `cp include/secrets.example.h include/secrets.h` and fill in WiFi, the printer IP/serial/access code, and the bridge URL.
3. **Run the bridge** on a machine where Claude Code is logged in:
   ```sh
   python3 bridge/claude_usage_bridge.py          # serves :8787/claude.json
   python3 bridge/claude_usage_bridge.py --once   # test
   ```
4. **Flash:**
   ```sh
   pio run -e esp32dev -t upload && pio device monitor   # DevKit V1
   pio run -e esp32s3  -t upload && pio device monitor   # S3 + screens
   ```
5. Open **http://desk-gauge.local/** to see both gauge faces live.

## Notes

- **Bambu:** the ESP connects directly to the printer (`bblp` / access code). If it can't connect on newer firmware, turn on *LAN Only* + *Developer Mode* on the printer. To see the printer's raw reports in the serial monitor (useful for checking P2S field names), set `BAMBU_DUMP_RAW 1` in `config.h`.
- **Claude limits:** the bridge uses the same undocumented endpoint as Claude Code's `/usage`, logged in with your local Claude Code session. The token stays on the bridge machine. If the bridge reports `401`, open Claude Code once to refresh the login.
- **Tokens today:** counted from `~/.claude/projects/**/*.jsonl` since local midnight, including cache reads (which make up most of the total).
