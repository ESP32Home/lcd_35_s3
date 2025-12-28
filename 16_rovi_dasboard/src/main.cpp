/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>
#include <lvgl.h>

#include <LiveDashboard.h>

#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include <FFat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <SD_MMC.h>

#define GFX_BL 6

#define SPI_MISO 2
#define SPI_MOSI 1
#define SPI_SCLK 5

#define LCD_CS -1
#define LCD_DC 3
#define LCD_RST -1
#define LCD_HOR_RES 320
#define LCD_VER_RES 480

#define I2C_SDA 8
#define I2C_SCL 7

static constexpr int kSdClk = 11;
static constexpr int kSdCmd = 10;
static constexpr int kSdD0 = 9;
static const char *kConfigPath = "/config.json";

TCA9554 TCA(0x20);

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC /* DC */, LCD_CS /* CS */, SPI_SCLK /* SCK */, SPI_MOSI /* MOSI */, SPI_MISO /* MISO */);
Arduino_GFX *gfx = new Arduino_ST7796(bus, LCD_RST /* RST */, 0 /* rotation */, true, LCD_HOR_RES, LCD_VER_RES);

TouchDrvFT6X36 touch;

uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_disp_draw_buf_t draw_buf;
lv_color_t *disp_draw_buf1;
lv_color_t *disp_draw_buf2;
lv_disp_drv_t disp_drv;

extern void lv_fs_fatfs_init(void);

static void rovi_format_voltage(char *buf, size_t buf_size, int32_t voltage_x10);

static live_dashboard::LiveDashboard g_dashboard;

struct RoviConfig {
  static constexpr size_t kMaxStages = 8;

  char robot_name[32]{};
  bool dark_theme = false;
  uint32_t stale_timeout_ms = 0;

  char splash_path[64]{};
  uint32_t splash_duration_ms = 0;

  int32_t voltage_min_x10 = 0;
  int32_t voltage_max_x10 = 0;
  int32_t voltage_initial_x10 = 0;
  char voltage_min_label[16]{};
  char voltage_max_label[16]{};
  live_dashboard::Stage voltage_stages[kMaxStages]{};
  size_t voltage_stage_count = 0;

  int32_t cpu_min = 0;
  int32_t cpu_max = 0;
  int32_t cpu_initial = 0;
  char cpu_min_label[16]{};
  char cpu_max_label[16]{};
  lv_color_t cpu_accent = lv_color_black();
};

static RoviConfig g_config;
static bool g_config_loaded = false;
static char g_config_error[128]{};

static void rovi_copy_cstr(char *dst, size_t dst_size, const char *src) {
  if (dst == nullptr || dst_size == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_size, "%s", src);
}

static int rovi_stricmp(const char *a, const char *b) {
  if (a == nullptr && b == nullptr) {
    return 0;
  }
  if (a == nullptr) {
    return -1;
  }
  if (b == nullptr) {
    return 1;
  }
  while (*a != '\0' && *b != '\0') {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z') {
      ca = static_cast<char>(ca - 'A' + 'a');
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = static_cast<char>(cb - 'A' + 'a');
    }
    if (ca != cb) {
      return static_cast<unsigned char>(ca) - static_cast<unsigned char>(cb);
    }
    a++;
    b++;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

static bool rovi_parse_lv_color(const char *value, lv_color_t *out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }

  if (value[0] == '#') {
    char *end = nullptr;
    uint32_t rgb = strtoul(value + 1, &end, 16);
    if (end == value + 1) {
      return false;
    }
    *out = lv_color_hex(rgb);
    return true;
  }

  if ((value[0] == '0') && (value[1] == 'x' || value[1] == 'X')) {
    char *end = nullptr;
    uint32_t rgb = strtoul(value + 2, &end, 16);
    if (end == value + 2) {
      return false;
    }
    *out = lv_color_hex(rgb);
    return true;
  }

  if (rovi_stricmp(value, "green") == 0) {
    *out = lv_palette_main(LV_PALETTE_GREEN);
    return true;
  }
  if (rovi_stricmp(value, "amber") == 0) {
    *out = lv_palette_main(LV_PALETTE_AMBER);
    return true;
  }
  if (rovi_stricmp(value, "orange") == 0) {
    *out = lv_palette_main(LV_PALETTE_ORANGE);
    return true;
  }
  if (rovi_stricmp(value, "red") == 0) {
    *out = lv_palette_main(LV_PALETTE_RED);
    return true;
  }
  if (rovi_stricmp(value, "blue") == 0) {
    *out = lv_palette_main(LV_PALETTE_BLUE);
    return true;
  }
  if (rovi_stricmp(value, "cyan") == 0) {
    *out = lv_palette_main(LV_PALETTE_CYAN);
    return true;
  }
  if (rovi_stricmp(value, "purple") == 0) {
    *out = lv_palette_main(LV_PALETTE_PURPLE);
    return true;
  }
  if (rovi_stricmp(value, "teal") == 0) {
    *out = lv_palette_main(LV_PALETTE_TEAL);
    return true;
  }
  if (rovi_stricmp(value, "yellow") == 0) {
    *out = lv_palette_main(LV_PALETTE_YELLOW);
    return true;
  }
  if (rovi_stricmp(value, "grey") == 0 || rovi_stricmp(value, "gray") == 0) {
    *out = lv_palette_main(LV_PALETTE_GREY);
    return true;
  }
  if (rovi_stricmp(value, "white") == 0) {
    *out = lv_color_white();
    return true;
  }

  return false;
}

static void rovi_sort_stages_desc(live_dashboard::Stage *stages, size_t stage_count) {
  if (stages == nullptr || stage_count < 2) {
    return;
  }

  for (size_t i = 0; i < stage_count; i++) {
    for (size_t j = 0; j + 1 < stage_count; j++) {
      if (stages[j].threshold < stages[j + 1].threshold) {
        live_dashboard::Stage tmp = stages[j];
        stages[j] = stages[j + 1];
        stages[j + 1] = tmp;
      }
    }
  }
}

static bool rovi_config_load_from_ffat(RoviConfig &cfg, const char *path, char *out_error, size_t out_error_size) {
  if (path == nullptr) {
    snprintf(out_error, out_error_size, "No config path");
    return false;
  }

  File f = FFat.open(path, "r");
  if (!f) {
    snprintf(out_error, out_error_size, "Config not found: %s", path);
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  if (err) {
    snprintf(out_error, out_error_size, "Config JSON parse error: %s", err.c_str());
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    snprintf(out_error, out_error_size, "Config root is not an object");
    return false;
  }

  const char *robot_name = root["robot_name"];
  if (robot_name != nullptr) {
    rovi_copy_cstr(cfg.robot_name, sizeof(cfg.robot_name), robot_name);
  } else {
    snprintf(out_error, out_error_size, "Missing key: robot_name");
    return false;
  }

  JsonObject ui = root["ui"].as<JsonObject>();
  if (!ui.isNull()) {
    if (!ui["dark_theme"].is<bool>()) {
      snprintf(out_error, out_error_size, "Missing/invalid: ui.dark_theme");
      return false;
    }
    cfg.dark_theme = ui["dark_theme"].as<bool>();

    if (!ui["stale_timeout_ms"].is<uint32_t>()) {
      snprintf(out_error, out_error_size, "Missing/invalid: ui.stale_timeout_ms");
      return false;
    }
    cfg.stale_timeout_ms = ui["stale_timeout_ms"].as<uint32_t>();
    if (cfg.stale_timeout_ms == 0) {
      snprintf(out_error, out_error_size, "Invalid: ui.stale_timeout_ms must be > 0");
      return false;
    }

    JsonObject splash = ui["splash"].as<JsonObject>();
    if (!splash.isNull()) {
      const char *splash_path = splash["path"];
      if (splash_path != nullptr) {
        rovi_copy_cstr(cfg.splash_path, sizeof(cfg.splash_path), splash_path);
      } else {
        snprintf(out_error, out_error_size, "Missing: ui.splash.path");
        return false;
      }

      if (!splash["duration_ms"].is<uint32_t>()) {
        snprintf(out_error, out_error_size, "Missing/invalid: ui.splash.duration_ms");
        return false;
      }
      cfg.splash_duration_ms = splash["duration_ms"].as<uint32_t>();
    } else {
      snprintf(out_error, out_error_size, "Missing object: ui.splash");
      return false;
    }
  } else {
    snprintf(out_error, out_error_size, "Missing object: ui");
    return false;
  }

  JsonObject gauges = root["gauges"].as<JsonObject>();
  if (!gauges.isNull()) {
    JsonObject voltage = gauges["voltage"].as<JsonObject>();
    if (!voltage.isNull()) {
      if (!voltage["min_x10"].is<int32_t>() || !voltage["max_x10"].is<int32_t>() || !voltage["initial_x10"].is<int32_t>()) {
        snprintf(out_error, out_error_size, "Missing/invalid: gauges.voltage.(min_x10/max_x10/initial_x10)");
        return false;
      }
      cfg.voltage_min_x10 = voltage["min_x10"].as<int32_t>();
      cfg.voltage_max_x10 = voltage["max_x10"].as<int32_t>();
      cfg.voltage_initial_x10 = voltage["initial_x10"].as<int32_t>();

      const char *voltage_min_label = voltage["min_label"];
      if (voltage_min_label != nullptr) {
        rovi_copy_cstr(cfg.voltage_min_label, sizeof(cfg.voltage_min_label), voltage_min_label);
      } else {
        snprintf(out_error, out_error_size, "Missing: gauges.voltage.min_label");
        return false;
      }
      const char *voltage_max_label = voltage["max_label"];
      if (voltage_max_label != nullptr) {
        rovi_copy_cstr(cfg.voltage_max_label, sizeof(cfg.voltage_max_label), voltage_max_label);
      } else {
        snprintf(out_error, out_error_size, "Missing: gauges.voltage.max_label");
        return false;
      }

      JsonArray stages = voltage["stages"].as<JsonArray>();
      if (!stages.isNull()) {
        size_t stage_count = 0;
        for (JsonVariant stage_v : stages) {
          if (stage_count >= RoviConfig::kMaxStages) {
            break;
          }

          JsonObject stage = stage_v.as<JsonObject>();
          if (stage.isNull()) {
            continue;
          }

          int32_t threshold = 0;
          bool has_threshold = false;
          if (stage["t"].is<int32_t>()) {
            threshold = stage["t"].as<int32_t>();
            has_threshold = true;
          } else if (stage["threshold"].is<int32_t>()) {
            threshold = stage["threshold"].as<int32_t>();
            has_threshold = true;
          }

          const char *color_str = nullptr;
          if (stage["c"].is<const char *>()) {
            color_str = stage["c"].as<const char *>();
          } else if (stage["color"].is<const char *>()) {
            color_str = stage["color"].as<const char *>();
          }

          lv_color_t color;
          if (!has_threshold || color_str == nullptr || !rovi_parse_lv_color(color_str, &color)) {
            continue;
          }

          cfg.voltage_stages[stage_count] = {threshold, color};
          stage_count++;
        }

        if (stage_count > 0) {
          cfg.voltage_stage_count = stage_count;
          rovi_sort_stages_desc(cfg.voltage_stages, cfg.voltage_stage_count);
        } else {
          snprintf(out_error, out_error_size, "No valid gauges.voltage.stages entries");
          return false;
        }
      } else {
        snprintf(out_error, out_error_size, "Missing array: gauges.voltage.stages");
        return false;
      }
    } else {
      snprintf(out_error, out_error_size, "Missing object: gauges.voltage");
      return false;
    }

    JsonObject cpu = gauges["cpu"].as<JsonObject>();
    if (!cpu.isNull()) {
      if (!cpu["min"].is<int32_t>() || !cpu["max"].is<int32_t>() || !cpu["initial"].is<int32_t>()) {
        snprintf(out_error, out_error_size, "Missing/invalid: gauges.cpu.(min/max/initial)");
        return false;
      }
      cfg.cpu_min = cpu["min"].as<int32_t>();
      cfg.cpu_max = cpu["max"].as<int32_t>();
      cfg.cpu_initial = cpu["initial"].as<int32_t>();

      const char *cpu_min_label = cpu["min_label"];
      if (cpu_min_label != nullptr) {
        rovi_copy_cstr(cfg.cpu_min_label, sizeof(cfg.cpu_min_label), cpu_min_label);
      } else {
        snprintf(out_error, out_error_size, "Missing: gauges.cpu.min_label");
        return false;
      }
      const char *cpu_max_label = cpu["max_label"];
      if (cpu_max_label != nullptr) {
        rovi_copy_cstr(cfg.cpu_max_label, sizeof(cfg.cpu_max_label), cpu_max_label);
      } else {
        snprintf(out_error, out_error_size, "Missing: gauges.cpu.max_label");
        return false;
      }

      const char *cpu_accent = cpu["accent"];
      if (cpu_accent != nullptr) {
        lv_color_t color;
        if (rovi_parse_lv_color(cpu_accent, &color)) {
          cfg.cpu_accent = color;
        } else {
          snprintf(out_error, out_error_size, "Invalid gauges.cpu.accent: %s", cpu_accent);
          return false;
        }
      } else {
        snprintf(out_error, out_error_size, "Missing: gauges.cpu.accent");
        return false;
      }
    } else {
      snprintf(out_error, out_error_size, "Missing object: gauges.cpu");
      return false;
    }
  } else {
    snprintf(out_error, out_error_size, "Missing object: gauges");
    return false;
  }

  if (cfg.voltage_min_x10 >= cfg.voltage_max_x10) {
    snprintf(out_error, out_error_size, "Invalid voltage range: min_x10 >= max_x10");
    return false;
  }
  if (cfg.voltage_initial_x10 < cfg.voltage_min_x10 || cfg.voltage_initial_x10 > cfg.voltage_max_x10) {
    snprintf(out_error, out_error_size, "Invalid voltage initial_x10 (out of range)");
    return false;
  }

  if (cfg.cpu_min >= cfg.cpu_max) {
    snprintf(out_error, out_error_size, "Invalid CPU range: min >= max");
    return false;
  }
  if (cfg.cpu_initial < cfg.cpu_min || cfg.cpu_initial > cfg.cpu_max) {
    snprintf(out_error, out_error_size, "Invalid CPU initial (out of range)");
    return false;
  }

  return true;
}

static void rovi_show_empty_dashboard(const char *fatal_message) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x111827), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x0B1220), LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(panel, 16, LV_PART_MAIN);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "CONFIG ERROR");
  lv_obj_set_style_text_color(title, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *details = lv_label_create(panel);
  lv_obj_set_width(details, LV_PCT(100));
  lv_obj_set_style_text_align(details, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(details, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_set_style_text_font(details, &lv_font_montserrat_14, LV_PART_MAIN);

  char msg[256];
  snprintf(msg, sizeof(msg),
           "%s\n\nUpload FS:\npio run -e esp32-s3-touch-lcd-35 -t uploadfs\n\nThen reboot.",
           (fatal_message != nullptr && fatal_message[0] != '\0') ? fatal_message : "Config not loaded");
  lv_label_set_text(details, msg);
  lv_obj_align(details, LV_ALIGN_CENTER, 0, 0);
}

static void rovi_demo_timer_cb(lv_timer_t *) {
  uint32_t now = millis();

  static uint32_t last_voltage_publish_ms = 0;
  static size_t voltage_idx = 0;
  static const int32_t voltage_values_x10[] = {
    126, // full
    122, // good
    111, // nominal
    110, // recharge soon
    106, // low
    95,  // critical
    120, // recovery
  };

  static uint32_t last_cpu_publish_ms = 0;
  static size_t cpu_idx = 0;
  static const int32_t cpu_values[] = {12, 37, 78, 5, 55};

  if (now - last_voltage_publish_ms >= 1200) {
    voltage_idx = (voltage_idx + 1) % (sizeof(voltage_values_x10) / sizeof(voltage_values_x10[0]));
    char voltage_buf[16];
    rovi_format_voltage(voltage_buf, sizeof(voltage_buf), voltage_values_x10[voltage_idx]);
    g_dashboard.publishVoltage(voltage_values_x10[voltage_idx], voltage_buf, now);
    last_voltage_publish_ms = now;
  }

  if (now - last_cpu_publish_ms >= 7000) {
    cpu_idx = (cpu_idx + 1) % (sizeof(cpu_values) / sizeof(cpu_values[0]));
    char cpu_buf[16];
    snprintf(cpu_buf, sizeof(cpu_buf), "%ld%%", static_cast<long>(cpu_values[cpu_idx]));
    g_dashboard.publishCpu(cpu_values[cpu_idx], cpu_buf, now);
    last_cpu_publish_ms = now;
  }
}

static bool rovi_sd_init(void) {
  if (!SD_MMC.setPins(kSdClk, kSdCmd, kSdD0)) {
    Serial.println("SD_MMC.setPins failed, skipping splash");
    return false;
  }

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC.begin failed, skipping splash");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card detected, skipping splash");
    return false;
  }

  return true;
}

static void rovi_show_splash_from_sd(const char *lvgl_path, uint32_t duration_ms) {
  if (!live_dashboard::ShowSplashFromLvglPath(lvgl_path,
                                             static_cast<lv_coord_t>(screenWidth),
                                             static_cast<lv_coord_t>(screenHeight),
                                             duration_ms)) {
    Serial.printf("Splash image not found/decodable: %s\n", lvgl_path);
  }
}

static void rovi_power_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  const char *action = static_cast<const char *>(lv_event_get_user_data(e));
  if (action == nullptr) {
    return;
  }

  Serial.printf("ROVI action requested: %s\n", action);

  static const char *btns[] = {"OK", ""};
  lv_obj_t *mbox = lv_msgbox_create(nullptr, g_config.robot_name, action, btns, true);
  lv_obj_center(mbox);
}

static void rovi_format_voltage(char *buf, size_t buf_size, int32_t voltage_x10) {
  if (buf == nullptr || buf_size == 0) {
    return;
  }

  int32_t whole = voltage_x10 / 10;
  int32_t frac = voltage_x10 % 10;
  if (frac < 0) {
    frac = -frac;
  }

  snprintf(buf, buf_size, "%ld.%01ldV", static_cast<long>(whole), static_cast<long>(frac));
}

void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp_drv);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;

  int16_t x[1], y[1];
  uint8_t touched = touch.getPoint(x, y, 1);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x[0];
    data->point.y = y[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static void lcd_reset(void) {
  TCA.write1(1, 1);
  delay(10);
  TCA.write1(1, 0);
  delay(10);
  TCA.write1(1, 1);
  delay(200);
}

void setup() {
  Serial.begin(115200);

  bool flashfs_ready = FFat.begin(false);
  if (!flashfs_ready) {
    snprintf(g_config_error, sizeof(g_config_error), "FFat mount failed (internal FATFS)");
  } else {
    g_config_loaded = rovi_config_load_from_ffat(g_config, kConfigPath, g_config_error, sizeof(g_config_error));
  }

  if (!g_config_loaded) {
    Serial.printf("FATAL: %s\n", g_config_error);
  } else {
    Serial.printf("Config loaded: %s\n", g_config.robot_name);
  }

  Wire.begin(I2C_SDA, I2C_SCL);

  TCA.begin();
  TCA.pinMode1(1, OUTPUT);
  lcd_reset();
  Serial.println("ROVI dashboard (demo) example");

  if (!touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
    Serial.println("Failed to find FT6X36 - check your wiring!");
    while (1) {
      delay(1000);
    }
  }

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  bool sd_ready = rovi_sd_init();

  lv_init();

  screenWidth = gfx->width();
  screenHeight = gfx->height();

  bufSize = screenWidth * 120;

  disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
  disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, bufSize);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  lv_fs_fatfs_init();

  if (!g_config_loaded) {
    rovi_show_empty_dashboard(g_config_error);
    Serial.println("Setup done (config error)");
    return;
  }

  if (sd_ready) {
    rovi_show_splash_from_sd(g_config.splash_path, g_config.splash_duration_ms);
  }

  char voltage_buf[16];
  rovi_format_voltage(voltage_buf, sizeof(voltage_buf), g_config.voltage_initial_x10);

  char cpu_buf[16];
  snprintf(cpu_buf, sizeof(cpu_buf), "%ld%%", static_cast<long>(g_config.cpu_initial));

  live_dashboard::LiveDashboardConfig dash_cfg{};
  dash_cfg.screen_width = static_cast<lv_coord_t>(screenWidth);
  dash_cfg.screen_height = static_cast<lv_coord_t>(screenHeight);
  dash_cfg.stale_timeout_ms = g_config.stale_timeout_ms;
  dash_cfg.dark_theme = g_config.dark_theme;

  dash_cfg.voltage_gauge.title = "Voltage";
  dash_cfg.voltage_gauge.min_value = g_config.voltage_min_x10;
  dash_cfg.voltage_gauge.max_value = g_config.voltage_max_x10;
  dash_cfg.voltage_gauge.initial_value = g_config.voltage_initial_x10;
  dash_cfg.voltage_gauge.initial_text = voltage_buf;
  dash_cfg.voltage_gauge.min_label = g_config.voltage_min_label;
  dash_cfg.voltage_gauge.max_label = g_config.voltage_max_label;
  dash_cfg.voltage_gauge.stages = g_config.voltage_stages;
  dash_cfg.voltage_gauge.stage_count = g_config.voltage_stage_count;
  dash_cfg.voltage_gauge.stages_fallback_color = lv_palette_main(LV_PALETTE_GREEN);

  dash_cfg.cpu_gauge.title = "CPU";
  dash_cfg.cpu_gauge.min_value = g_config.cpu_min;
  dash_cfg.cpu_gauge.max_value = g_config.cpu_max;
  dash_cfg.cpu_gauge.initial_value = g_config.cpu_initial;
  dash_cfg.cpu_gauge.initial_text = cpu_buf;
  dash_cfg.cpu_gauge.min_label = g_config.cpu_min_label;
  dash_cfg.cpu_gauge.max_label = g_config.cpu_max_label;
  dash_cfg.cpu_gauge.accent_color = g_config.cpu_accent;

  dash_cfg.shutdown_button.tile_title = "Power";
  dash_cfg.shutdown_button.button_label = "Shutdown";
  dash_cfg.shutdown_button.button_color = lv_palette_main(LV_PALETTE_RED);

  dash_cfg.restart_button.tile_title = "System";
  dash_cfg.restart_button.button_label = "Restart";
  dash_cfg.restart_button.button_color = lv_palette_main(LV_PALETTE_BLUE);

  dash_cfg.info_tile.title = g_config.robot_name;
  dash_cfg.info_tile.subtitle = "Dashboard (demo timer)";
  dash_cfg.info_tile.body = "Demo timer updates\nNo live ROS data yet";

  dash_cfg.ros_tile.title = "ROS";
  dash_cfg.ros_tile.status = "Status: demo/offline";
  dash_cfg.ros_tile.status_color = lv_palette_main(LV_PALETTE_RED);
  dash_cfg.ros_tile.body = "Next: subscribe topics\nand drive gauges";

  g_dashboard.create(dash_cfg);

  static char shutdown_msg[] = "Shutdown requested (demo only)";
  lv_obj_add_event_cb(g_dashboard.shutdownButton(), rovi_power_btn_event_cb, LV_EVENT_CLICKED, shutdown_msg);
  static char restart_msg[] = "Restart requested (demo only)";
  lv_obj_add_event_cb(g_dashboard.restartButton(), rovi_power_btn_event_cb, LV_EVENT_CLICKED, restart_msg);

  lv_timer_create(rovi_demo_timer_cb, 200, nullptr);

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  g_dashboard.tick();
  delay(1);
}
