#include "WsLcd35S3Hal.h"

#include <Arduino.h>
#include <FFat.h>
#include <Wire.h>

#include <cstdio>
#include <new>

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"

#include "esp_heap_caps.h"

namespace ws_lcd_35_s3_hal {
namespace {

// Board pins / wiring (matches existing examples in this repo)
static constexpr int kBacklightPin = 6;

static constexpr int kSpiMiso = 2;
static constexpr int kSpiMosi = 1;
static constexpr int kSpiSclk = 5;

static constexpr int kLcdCs = -1;
static constexpr int kLcdDc = 3;
static constexpr int kLcdRst = -1;
static constexpr int kLcdHorRes = 320;
static constexpr int kLcdVerRes = 480;

static constexpr int kI2cSda = 8;
static constexpr int kI2cScl = 7;

struct ArduinoFsFile {
  fs::File file;
};

TCA9554 g_tca(0x20);
TouchDrvFT6X36 g_touch;

Arduino_ESP32SPI g_bus(kLcdDc, kLcdCs, kSpiSclk, kSpiMosi, kSpiMiso);
Arduino_ST7796 g_gfx(&g_bus, kLcdRst, 0 /* rotation */, true /* IPS? */, kLcdHorRes, kLcdVerRes);

lv_disp_draw_buf_t g_draw_buf;
lv_color_t *g_disp_draw_buf1 = nullptr;
lv_color_t *g_disp_draw_buf2 = nullptr;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;

void lcd_reset() {
  g_tca.write1(1, 1);
  delay(10);
  g_tca.write1(1, 0);
  delay(10);
  g_tca.write1(1, 1);
  delay(200);
}

static void disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  g_gfx.draw16bitBeRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&color_p->full), w, h);
#else
  g_gfx.draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&color_p->full), w, h);
#endif

  lv_disp_flush_ready(disp_drv);
}

static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  int16_t x[1], y[1];
  uint8_t touched = g_touch.getPoint(x, y, 1);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x[0];
    data->point.y = y[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void *lvgl_fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
  if (drv == nullptr || path == nullptr) {
    return nullptr;
  }

  fs::FS *fs = static_cast<fs::FS *>(drv->user_data);
  if (fs == nullptr) {
    return nullptr;
  }

  const char *open_mode = "r";
  if (mode == LV_FS_MODE_WR) {
    open_mode = "w";
  } else if (mode == (LV_FS_MODE_RD | LV_FS_MODE_WR)) {
    open_mode = "r+";
  }

  char normalized[128];
  const char *open_path = path;
  if (path[0] != '/') {
    snprintf(normalized, sizeof(normalized), "/%s", path);
    open_path = normalized;
  }

  fs::File file = fs->open(open_path, open_mode);
  if (!file) {
    return nullptr;
  }

  void *mem = lv_mem_alloc(sizeof(ArduinoFsFile));
  if (mem == nullptr) {
    file.close();
    return nullptr;
  }

  ArduinoFsFile *handle = new (mem) ArduinoFsFile();
  handle->file = file;
  return handle;
}

static lv_fs_res_t lvgl_fs_close_cb(lv_fs_drv_t *, void *file_p) {
  if (file_p == nullptr) {
    return LV_FS_RES_OK;
  }

  ArduinoFsFile *handle = static_cast<ArduinoFsFile *>(file_p);
  handle->file.close();
  handle->~ArduinoFsFile();
  lv_mem_free(handle);
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_fs_read_cb(lv_fs_drv_t *, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
  if (file_p == nullptr || buf == nullptr || br == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  ArduinoFsFile *handle = static_cast<ArduinoFsFile *>(file_p);
  *br = static_cast<uint32_t>(handle->file.read(static_cast<uint8_t *>(buf), btr));
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_fs_write_cb(lv_fs_drv_t *, void *file_p, const void *buf, uint32_t btw, uint32_t *bw) {
  if (file_p == nullptr || buf == nullptr || bw == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  ArduinoFsFile *handle = static_cast<ArduinoFsFile *>(file_p);
  *bw = static_cast<uint32_t>(handle->file.write(static_cast<const uint8_t *>(buf), btw));
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_fs_seek_cb(lv_fs_drv_t *, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
  if (file_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  ArduinoFsFile *handle = static_cast<ArduinoFsFile *>(file_p);
  uint32_t target = pos;
  if (whence == LV_FS_SEEK_CUR) {
    target = static_cast<uint32_t>(handle->file.position()) + pos;
  } else if (whence == LV_FS_SEEK_END) {
    target = static_cast<uint32_t>(handle->file.size()) + pos;
  }

  if (!handle->file.seek(target)) {
    return LV_FS_RES_UNKNOWN;
  }
  return LV_FS_RES_OK;
}

static lv_fs_res_t lvgl_fs_tell_cb(lv_fs_drv_t *, void *file_p, uint32_t *pos_p) {
  if (file_p == nullptr || pos_p == nullptr) {
    return LV_FS_RES_INV_PARAM;
  }

  ArduinoFsFile *handle = static_cast<ArduinoFsFile *>(file_p);
  *pos_p = static_cast<uint32_t>(handle->file.position());
  return LV_FS_RES_OK;
}

} // namespace

WsLcd35S3Hal::WsLcd35S3Hal() : flash_fs_(&FFat) {}

bool WsLcd35S3Hal::begin() {
  Wire.begin(kI2cSda, kI2cScl);

  g_tca.begin();
  g_tca.pinMode1(1, OUTPUT);
  lcd_reset();

  if (!initTouch_()) {
    Serial.println("FATAL: Touch init failed");
    return false;
  }
  if (!initDisplay_()) {
    Serial.println("FATAL: Display init failed");
    return false;
  }

  flashfs_mounted_ = initFlashFs_();

  lv_init();

  screen_width_ = static_cast<uint16_t>(g_gfx.width());
  screen_height_ = static_cast<uint16_t>(g_gfx.height());

  const uint32_t buf_pixels = static_cast<uint32_t>(screen_width_) * 120U;
  g_disp_draw_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT));
  g_disp_draw_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT));
  if (g_disp_draw_buf1 == nullptr || g_disp_draw_buf2 == nullptr) {
    Serial.println("FATAL: LVGL draw buffers alloc failed");
    return false;
  }
  lv_disp_draw_buf_init(&g_draw_buf, g_disp_draw_buf1, g_disp_draw_buf2, buf_pixels);

  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = screen_width_;
  g_disp_drv.ver_res = screen_height_;
  g_disp_drv.flush_cb = disp_flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  g_disp_drv.user_data = this;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = touch_read_cb;
  g_indev_drv.user_data = this;
  lv_indev_drv_register(&g_indev_drv);

  if (flashfs_mounted_) {
    registerFlashFsWithLvgl_(lvgl_flash_drive_letter_);
  } else {
    Serial.println("WARN: FFat not mounted (no LVGL flash FS)");
  }

  return true;
}

void WsLcd35S3Hal::loop() {
  lv_timer_handler();
  delay(1);
}

bool WsLcd35S3Hal::initDisplay_() {
  if (!g_gfx.begin()) {
    Serial.println("gfx.begin() failed");
    return false;
  }

  g_gfx.fillScreen(RGB565_BLACK);

  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, HIGH);
  return true;
}

bool WsLcd35S3Hal::initTouch_() {
  if (!g_touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
    Serial.println("Failed to find FT6X36 - check wiring");
    return false;
  }
  return true;
}

bool WsLcd35S3Hal::initFlashFs_() {
  if (!FFat.begin(false)) {
    Serial.println("FFat.begin(false) failed");
    return false;
  }
  return true;
}

void WsLcd35S3Hal::registerFlashFsWithLvgl_(char drive_letter) {
  static lv_fs_drv_t fs_drv;
  lv_fs_drv_init(&fs_drv);
  fs_drv.letter = drive_letter;
  fs_drv.cache_size = 0;
  fs_drv.user_data = flash_fs_;

  fs_drv.open_cb = lvgl_fs_open_cb;
  fs_drv.close_cb = lvgl_fs_close_cb;
  fs_drv.read_cb = lvgl_fs_read_cb;
  fs_drv.write_cb = lvgl_fs_write_cb;
  fs_drv.seek_cb = lvgl_fs_seek_cb;
  fs_drv.tell_cb = lvgl_fs_tell_cb;

  lv_fs_drv_register(&fs_drv);
}

} // namespace ws_lcd_35_s3_hal
