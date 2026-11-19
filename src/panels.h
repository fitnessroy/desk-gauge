#pragma once
// Physical round panel (ESP32-S3 build only).
//
// DIYUSER 1.8" round 360x360, ST77916 controller over QSPI (4 data lines, no DC pin).
// Header: GND VCC SCL IO0 IO1 IO2 IO3 RST CS BL TE. TE is left unconnected.
//
// Driven with ESP-IDF's spi_master the way Slmsim's ST77916 player does it on this module:
// registers as 0x02 + 24-bit address (cmd << 8) with parameters on IO0; a frame as RAMWR
// (0x2C), then one CS-low burst whose first chunk is 0x32 + 0x3C00 and the rest bare data on
// four lines. LovyanGFX 1.3.0's Panel_ST77916 woke the panel but could not place pixels, so
// LovyanGFX only draws into sprites.
#include <Arduino.h>
#include <driver/spi_master.h>

#include "config.h"

struct PanelPins {
  int sclk, io0, io1, io2, io3, cs, rst, bl;
};

class RoundPanel {
 public:
  RoundPanel(spi_host_device_t host, const PanelPins& p, int pwmCh) : _host(host), _p(p), _pwm(pwmCh) {}

  bool init();
  void setBrightness(uint8_t level);
  void fill(uint16_t rgb565);
  // A whole frame as LGFX_Sprite stores it (byte-swapped RGB565). It may live in PSRAM.
  void pushFrame(const uint16_t* px) { pushRows(px, 0, PANEL_H - 1); }
  // Only rows y0..y1 of such a frame (`px` is still the whole frame).
  void pushRows(const uint16_t* px, int y0, int y1);

 private:
  static constexpr int kBandBytes = 8192;  // Slmsim's chunk size

  void writeReg(uint8_t cmd, const uint8_t* data = nullptr, size_t len = 0);
  void setRowWindow(int y0, int y1);
  void beginPixels(int y0 = 0, int y1 = PANEL_H - 1);
  void writePixels(const uint8_t* data, size_t len);
  void endPixels();

  spi_host_device_t _host;
  PanelPins _p;
  int _pwm;
  spi_device_handle_t _dev = nullptr;      // 8-bit command + 24-bit address
  spi_device_handle_t _devData = nullptr;  // bare data, same bus
  uint8_t* _band = nullptr;  // DMA-capable bounce buffer in internal RAM
  bool _firstChunk = true;
};
