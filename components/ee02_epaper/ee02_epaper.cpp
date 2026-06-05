#include "ee02_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace ee02_epaper {

static const char *const TAG = "ee02_epaper";

size_t EE02Epaper::get_buffer_length_() {
  // One nibble per pixel => width*height/2 bytes == 960000 for this panel.
  return (size_t) EE02_WIDTH * (size_t) EE02_HEIGHT / 2;
}

void EE02Epaper::setup() {
  ESP_LOGCONFIG(TAG, "Setting up EE02 ePaper...");

  this->reset_pin_->setup();
  this->dc_pin_->setup();
  this->busy_pin_->setup();
  this->power_pin_->setup();
  this->cs_master_pin_->setup();
  this->cs_slave_pin_->setup();

  this->reset_pin_->digital_write(false);
  this->dc_pin_->digital_write(false);
  this->cs_master_pin_->digital_write(true);
  this->cs_slave_pin_->digital_write(true);
  this->power_pin_->digital_write(true);

  this->spi_setup();

  this->init_internal_(this->get_buffer_length_());
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate framebuffer (%u bytes) - is PSRAM enabled?",
             (unsigned) this->get_buffer_length_());
    this->mark_failed();
    return;
  }
  // 0x11 => both nibbles white
  memset(this->buffer_, (E6_WHITE << 4) | E6_WHITE, this->get_buffer_length_());

  this->init_display_();
  ESP_LOGCONFIG(TAG, "EE02 ePaper setup complete");
}

uint8_t EE02Epaper::color_to_e6_(Color color) {
  struct Ref { uint8_t code; uint8_t r, g, b; };
  static const Ref refs[] = {
      {E6_BLACK, 0, 0, 0},      {E6_WHITE, 255, 255, 255},
      {E6_YELLOW, 255, 255, 0}, {E6_RED, 255, 0, 0},
      {E6_BLUE, 0, 0, 255},     {E6_GREEN, 0, 255, 0},
  };
  uint32_t best = 0xFFFFFFFF;
  uint8_t best_code = E6_WHITE;
  for (auto &ref : refs) {
    int dr = (int) color.r - ref.r;
    int dg = (int) color.g - ref.g;
    int db = (int) color.b - ref.b;
    uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
    if (d < best) { best = d; best_code = ref.code; }
  }
  return best_code;
}

void EE02Epaper::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EE02_WIDTH || y < 0 || y >= EE02_HEIGHT)
    return;
  uint8_t code = this->color_to_e6_(color);
  size_t idx = ((size_t) y * EE02_WIDTH + (size_t) x) / 2;
  if ((x & 1) == 0) {
    this->buffer_[idx] = (code << 4) | (this->buffer_[idx] & 0x0F);
  } else {
    this->buffer_[idx] = (this->buffer_[idx] & 0xF0) | (code & 0x0F);
  }
}

void EE02Epaper::update() {
  this->do_update_();   // run the user's lambda into the buffer
  this->display();      // push to panel
}

void EE02Epaper::show_next_solid() {
  uint8_t code = this->solid_code_ & 0x0F;
  ESP_LOGW(TAG, ">>> SOLID FILL: code %u <<<", (unsigned) code);
  memset(this->buffer_, (code << 4) | code, this->get_buffer_length_());
  this->display();
  this->solid_code_ = (this->solid_code_ + 1) & 0x07;  // cycle 0..7
}

void EE02Epaper::show_nibble_test() {
  ESP_LOGI(TAG, "Painting nibble test pattern (codes 0..7)...");
  // 8 vertical strips across the NATIVE portrait width (1200 px => 150 px each).
  // Strip s gets raw nibble code s in BOTH nibbles of every byte it covers.
  const uint16_t strip_w = EE02_WIDTH / 8;  // 150 px = 75 bytes
  for (uint16_t y = 0; y < EE02_HEIGHT; y++) {
    uint8_t *row = this->buffer_ + (size_t) y * EE02_BYTES_PER_ROW;
    for (uint16_t x = 0; x < EE02_WIDTH; x++) {
      uint8_t code = (x / strip_w) & 0x07;
      size_t bidx = x / 2;
      if ((x & 1) == 0) {
        row[bidx] = (code << 4) | (row[bidx] & 0x0F);
      } else {
        row[bidx] = (row[bidx] & 0xF0) | (code & 0x0F);
      }
    }
  }
  this->display();
}

void EE02Epaper::display() {
  ESP_LOGI(TAG, "Pushing framebuffer to panel...");
  const uint8_t *buf = this->buffer_;

  // ---- Master half ----
  ESP_LOGI(TAG, "Master half start");
  this->cs_master_pin_->digital_write(false);
  this->send_command_(DTM);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint16_t line = 0; line < EE02_HEIGHT; line++) {
    const uint8_t *row = buf + (size_t) line * EE02_BYTES_PER_ROW;
    this->write_array(row, EE02_HALF_ROW_BYTES);  // first 300 bytes of the row
    if ((line & 0x1F) == 0) App.feed_wdt();
  }
  this->disable();
  this->cs_master_pin_->digital_write(true);
  delay(50);

  // ---- Slave half ----
  ESP_LOGI(TAG, "Slave half start");
  this->cs_slave_pin_->digital_write(false);
  this->send_command_(DTM);
  this->dc_pin_->digital_write(true);
  this->enable();
  for (uint16_t line = 0; line < EE02_HEIGHT; line++) {
    const uint8_t *row = buf + (size_t) line * EE02_BYTES_PER_ROW;
    this->write_array(row + EE02_HALF_ROW_BYTES, EE02_HALF_ROW_BYTES);  // second 300 bytes
    if ((line & 0x1F) == 0) App.feed_wdt();
  }
  this->disable();
  this->cs_slave_pin_->digital_write(true);

  this->turn_on_();
  ESP_LOGI(TAG, "Framebuffer push complete");
}

void EE02Epaper::init_display_() {
  ESP_LOGI(TAG, "Initialising panel...");
  this->reset_pin_->digital_write(true);  delay(30);
  this->reset_pin_->digital_write(false); delay(30);
  this->reset_pin_->digital_write(true);  delay(30);
  this->reset_pin_->digital_write(false); delay(30);
  this->reset_pin_->digital_write(true);  delay(30);
  this->power_pin_->digital_write(true);

  const uint8_t AN_TM_V[] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
  const uint8_t CMD66_V[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
  const uint8_t PSR_V[] = {0xDF, 0x69};
  const uint8_t CDI_V[] = {0xF7};
  const uint8_t TCON_V[] = {0x03, 0x03};
  const uint8_t AGID_V[] = {0x10};
  const uint8_t PWS_V[] = {0x22};
  const uint8_t CCSET_V[] = {0x01};
  const uint8_t TRES_V[] = {0x04, 0xB0, 0x03, 0x20};
  const uint8_t PWR_V[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
  const uint8_t EN_BUF_V[] = {0x07};
  const uint8_t BTST_P_V[] = {0xE8, 0x28};
  const uint8_t BOOST_VDDP_EN_V[] = {0x01};
  const uint8_t BTST_N_V[] = {0xE8, 0x28};
  const uint8_t BUCK_BOOST_VDDN_V[] = {0x01};
  const uint8_t TFT_VCOM_POWER_V[] = {0x02};

  this->cs_master_pin_->digital_write(false);
  this->send_buf_(AN_TM, AN_TM_V, sizeof(AN_TM_V));
  this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(CMD66, CMD66_V, sizeof(CMD66_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(PSR, PSR_V, sizeof(PSR_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(CDI, CDI_V, sizeof(CDI_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(TCON, TCON_V, sizeof(TCON_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(AGID, AGID_V, sizeof(AGID_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(PWS, PWS_V, sizeof(PWS_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(CCSET, CCSET_V, sizeof(CCSET_V)); this->cs_both_(true);
  this->cs_both_(false); this->send_buf_(TRES, TRES_V, sizeof(TRES_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(PWR_EPD, PWR_V, sizeof(PWR_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(BTST_P, BTST_P_V, sizeof(BTST_P_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(BTST_N, BTST_N_V, sizeof(BTST_N_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V)); this->cs_both_(true);
  this->cs_master_pin_->digital_write(false); this->send_buf_(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V)); this->cs_both_(true);

  ESP_LOGI(TAG, "Panel initialised");
}

void EE02Epaper::send_command_(uint8_t command) {
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(command);
  this->disable();
}

void EE02Epaper::send_data_(uint8_t data) {
  this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(data);
  this->disable();
}

void EE02Epaper::send_buf_(uint8_t cmd, const uint8_t *buf, size_t len) {
  this->send_command_(cmd);
  for (size_t i = 0; i < len; i++) this->send_data_(buf[i]);
}

void EE02Epaper::cs_both_(bool high) {
  this->cs_master_pin_->digital_write(high);
  this->cs_slave_pin_->digital_write(high);
}

void EE02Epaper::wait_busy_() {
  // Reference firmware: LOW = busy, HIGH = idle.
  const uint32_t start = millis();
  while (!this->busy_pin_->digital_read()) {
    App.feed_wdt();
    delay(10);
    if (millis() - start > 60000) {
      ESP_LOGW(TAG, "Busy wait timed out; proceeding");
      break;
    }
  }
  delay(20);
}

void EE02Epaper::turn_on_() {
  ESP_LOGI(TAG, "Refresh (PON -> DRF -> POF)...");
  this->cs_both_(false); this->send_command_(PON); this->cs_both_(true);
  this->wait_busy_();
  delay(50);
  this->cs_both_(false); this->send_command_(DRF); this->send_data_(0x00); this->cs_both_(true);
  this->wait_busy_();
  this->cs_both_(false); this->send_command_(POF); this->send_data_(0x00); this->cs_both_(true);
  this->wait_busy_();
  ESP_LOGI(TAG, "Refresh complete");
}

void EE02Epaper::dump_config() {
  ESP_LOGCONFIG(TAG, "EE02 ePaper:");
  LOG_PIN("  Reset: ", this->reset_pin_);
  LOG_PIN("  DC: ", this->dc_pin_);
  LOG_PIN("  Busy: ", this->busy_pin_);
  LOG_PIN("  Power: ", this->power_pin_);
  LOG_PIN("  CS Master: ", this->cs_master_pin_);
  LOG_PIN("  CS Slave: ", this->cs_slave_pin_);
  ESP_LOGCONFIG(TAG, "  Resolution: %ux%u (portrait native)", EE02_WIDTH, EE02_HEIGHT);
}

}  // namespace ee02_epaper
}  // namespace esphome
