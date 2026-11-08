#pragma once
// Physical round panels (ESP32-S3 build only).
//
// Defaults to GC9A01 over 4-wire SPI, the most common AliExpress round panel.
// If your 1.8" panel is a different controller (e.g. ST77916 QSPI 360x360) this is the
// only file that needs to change.
#include <LovyanGFX.hpp>

#include "config.h"

class RoundPanel : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  RoundPanel(spi_host_device_t host, int sclk, int mosi, int dc, int cs, int rst, int bl, int pwmCh) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = host;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = sclk;
      cfg.pin_mosi = mosi;
      cfg.pin_miso = -1;
      cfg.pin_dc = dc;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = cs;
      cfg.pin_rst = rst;
      cfg.pin_busy = -1;
      cfg.memory_width = PANEL_W;
      cfg.memory_height = PANEL_H;
      cfg.panel_width = PANEL_W;
      cfg.panel_height = PANEL_H;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = bl;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = pwmCh;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
