# desk-gauge

A two-screen round desk gauge on an ESP32:

- **Screen 0 – Claude:** the outer ring is your 5-hour session limit, the inner ring is your weekly limit. It also shows reset countdowns and today's Claude Code token count.
- **Screen 1 – Bambu Lab printer (P2S, also X1/P1/A1):** state, progress ring, time left, layers, nozzle/bed temps and job name.

```
 Bambu printer ──MQTT/TLS :8883──┐
                                 ├──> ESP32 ──> 2× ST77916 QSPI screens
 claude_usage_bridge.py ─HTTP────┘        └──> http://desk-gauge.local/ (live preview)
 (Mac / k3s, reads Claude Code login)
```

## Hardware

| | Board | Screens |
|---|---|---|
| `esp32dev` | ESP32 DevKit V1 (WROOM-32) | none: use the web preview |
| `esp32s3` | ESP32-S3 DevKitC-1 **N16R8** | 2× 1.8\" ST77916 360×360 QSPI |

### Wiring (ESP32-S3)

Screens: DIYUSER 1.8" round, 360×360, **ST77916 over QSPI** (header: GND VCC SCL IO0 IO1 IO2 IO3 RST CS BL TE).
Each screen has its own QSPI bus (SPI2 / SPI3). The pins are set in `include/config.h` (`PANEL0_PINS`, `PANEL1_PINS`).

| Screen pin | Screen 0 (Claude) | Screen 1 (Printer) |
|---|---|---|
| GND | GND | GND |
| VCC | 3V3 | 3V3 |
| SCL | GPIO 12 | GPIO 41 |
| IO0 | GPIO 11 | GPIO 40 |
| IO1 | GPIO 13 | GPIO 39 |
| IO2 | GPIO 14 | GPIO 2 |
| IO3 | GPIO 9 | GPIO 1 |
| RST | GPIO 8 | GPIO 21 |
| CS | GPIO 10 | GPIO 42 |
| BL | GPIO 18 | GPIO 47 |
| TE | not connected | not connected |

> The driver is LovyanGFX `Panel_ST77916` (`-DLGFX_USE_QSPI`). Its start-up settings come from the Guition JC3636W518. If colours look wrong, flip `invert` / `rgb_order` in `src/panels.h`. If the screen stays black with the backlight on, this screen needs its seller's own start-up settings.

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
