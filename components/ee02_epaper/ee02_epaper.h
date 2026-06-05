#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace ee02_epaper {

// Panel geometry — NATIVE ORIENTATION IS PORTRAIT (verified from amir-hadi header).
static const uint16_t EE02_WIDTH = 1200;
static const uint16_t EE02_HEIGHT = 1600;
// 4 bits per pixel, two pixels per byte. Full row = 1200 px = 600 bytes,
// split into two 300-byte halves (master controller / slave controller).
static const uint16_t EE02_BYTES_PER_ROW = EE02_WIDTH / 2;        // 600
static const uint16_t EE02_HALF_ROW_BYTES = EE02_BYTES_PER_ROW / 2;  // 300

// Spectra 6 native 4-bit colour codes
enum E6Color : uint8_t {
  // Verified empirically via full-screen colour sweep on this panel:
  // 0=black 1=white 2=yellow 3=red 5=blue 6=green
  E6_BLACK = 0x0,
  E6_WHITE = 0x1,
  E6_YELLOW = 0x2,
  E6_RED = 0x3,
  E6_BLUE = 0x5,
  E6_GREEN = 0x6,
};

// Controller command constants — verified against amir-hadi/esphome-waveshare-13.3-epd
static const uint8_t PSR = 0x00;
static const uint8_t PWR_EPD = 0x01;
static const uint8_t POF = 0x02;
static const uint8_t PON = 0x04;
static const uint8_t BTST_N = 0x05;
static const uint8_t BTST_P = 0x06;
static const uint8_t DTM = 0x10;
static const uint8_t DRF = 0x12;
static const uint8_t CDI = 0x50;
static const uint8_t TCON = 0x60;
static const uint8_t TRES = 0x61;
static const uint8_t AN_TM = 0x74;
static const uint8_t AGID = 0x86;
static const uint8_t BUCK_BOOST_VDDN = 0xB0;
static const uint8_t TFT_VCOM_POWER = 0xB1;
static const uint8_t EN_BUF = 0xB6;
static const uint8_t BOOST_VDDP_EN = 0xB7;
static const uint8_t CCSET = 0xE0;
static const uint8_t PWS = 0xE3;
static const uint8_t CMD66 = 0xF0;

class EE02Epaper : public display::DisplayBuffer,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                         spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 public:
  void set_reset_pin(GPIOPin *pin) { reset_pin_ = pin; }
  void set_dc_pin(GPIOPin *pin) { dc_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { busy_pin_ = pin; }
  void set_power_pin(GPIOPin *pin) { power_pin_ = pin; }
  void set_cs_master_pin(GPIOPin *pin) { cs_master_pin_ = pin; }
  void set_cs_slave_pin(GPIOPin *pin) { cs_slave_pin_ = pin; }

  void setup() override;
  void dump_config() override;
  void update() override;
  void display();
  // DIAGNOSTIC: fill the buffer with raw nibble codes 0..7 as vertical strips,
  // bypassing colour quantization, then push. Lets us read the true colour map.
  void show_nibble_test();
  // DIAGNOSTIC: fill the WHOLE screen with one raw code; advances each call.
  void show_next_solid();
  uint8_t solid_code_{0};

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  int get_width_internal() override { return EE02_WIDTH; }
  int get_height_internal() override { return EE02_HEIGHT; }
  size_t get_buffer_length_();

  uint8_t color_to_e6_(Color color);

  void init_display_();
  void send_command_(uint8_t cmd);
  void send_data_(uint8_t data);
  void send_buf_(uint8_t cmd, const uint8_t *buf, size_t len);
  void cs_both_(bool high);
  void wait_busy_();
  void turn_on_();

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *dc_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *power_pin_{nullptr};
  GPIOPin *cs_master_pin_{nullptr};
  GPIOPin *cs_slave_pin_{nullptr};
};

}  // namespace ee02_epaper
}  // namespace esphome
