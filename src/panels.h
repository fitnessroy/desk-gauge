#pragma once
// Physical round panels (ESP32-S3 build only).
//
// DIYUSER 1.8" round 360x360, ST77916 controller over QSPI (4 data lines, no DC pin).
// Header: GND VCC SCL IO0 IO1 IO2 IO3 RST CS BL TE. TE is left unconnected.
// Based on LovyanGFX's lgfx_user/LGFX_JC3636W518.hpp (same controller). Requires -DLGFX_USE_QSPI.
#include <LovyanGFX.hpp>

#include "config.h"

struct PanelPins {
  int sclk, io0, io1, io2, io3, cs, rst, bl;
};

class RoundPanel : public lgfx::LGFX_Device {
  lgfx::Panel_ST77916 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  RoundPanel(spi_host_device_t host, const PanelPins& p, int pwmCh) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = host;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = p.sclk;
      cfg.pin_io0 = p.io0;
      cfg.pin_io1 = p.io1;
      cfg.pin_io2 = p.io2;
      cfg.pin_io3 = p.io3;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = p.cs;
      cfg.pin_rst = p.rst;
      cfg.pin_busy = -1;
      cfg.memory_width = cfg.panel_width = PANEL_W;
      cfg.memory_height = cfg.panel_height = PANEL_H;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;     // flip if colours look inverted
      cfg.rgb_order = true;  // flip if red and blue are swapped
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = p.bl;
      cfg.invert = false;
      cfg.freq = 5000;
      cfg.pwm_channel = pwmCh;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
