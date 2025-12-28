#pragma once

#include <cstdint>

#include <FS.h>

namespace ws_lcd_35_s3_hal {

class WsLcd35S3Hal {
public:
  WsLcd35S3Hal();

  bool begin();
  void loop();

  uint16_t width() const { return screen_width_; }
  uint16_t height() const { return screen_height_; }

  bool flashFsMounted() const { return flashfs_mounted_; }
  fs::FS &flashFs() { return *flash_fs_; }

  char lvglFlashDriveLetter() const { return lvgl_flash_drive_letter_; }

private:
  bool initDisplay_();
  bool initTouch_();
  bool initFlashFs_();
  void registerFlashFsWithLvgl_(char drive_letter);

  uint16_t screen_width_ = 0;
  uint16_t screen_height_ = 0;

  bool flashfs_mounted_ = false;
  fs::FS *flash_fs_ = nullptr;
  char lvgl_flash_drive_letter_ = 'F';
};

} // namespace ws_lcd_35_s3_hal
