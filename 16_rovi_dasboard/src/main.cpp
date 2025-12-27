/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>
#include <lvgl.h>

#include <Arduino_GFX_Library.h>
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include <stdio.h>
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

static constexpr uint32_t kValueStaleTimeoutMs = 5000;

static constexpr int32_t kVoltageMinX10 = 90;
static constexpr int32_t kVoltageMaxX10 = 130;
static constexpr int32_t kVoltageValueX10 = 121;

static constexpr int32_t kCpuMin = 0;
static constexpr int32_t kCpuMax = 100;
static constexpr int32_t kCpuValue = 37;

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

struct RoviGaugeStage {
  int32_t threshold;
  lv_color_t color;
};

static lv_color_t rovi_pick_stage_color(int32_t value, const RoviGaugeStage *stages, size_t stage_count, lv_color_t fallback) {
  if (stages == nullptr || stage_count == 0) {
    return fallback;
  }

  for (size_t i = 0; i < stage_count; i++) {
    if (value >= stages[i].threshold) {
      return stages[i].color;
    }
  }

  return stages[stage_count - 1].color;
}

static lv_obj_t *g_voltage_arc = nullptr;
static lv_obj_t *g_voltage_value_label = nullptr;
static lv_obj_t *g_cpu_arc = nullptr;
static lv_obj_t *g_cpu_value_label = nullptr;

static int32_t g_voltage_value_x10 = kVoltageValueX10;
static uint32_t g_voltage_last_update_ms = 0;
static bool g_voltage_has_value = false;
static bool g_voltage_is_stale = true;
static int32_t g_voltage_last_drawn_x10 = 0;

static int32_t g_cpu_value = kCpuValue;
static uint32_t g_cpu_last_update_ms = 0;
static bool g_cpu_has_value = false;
static bool g_cpu_is_stale = true;
static int32_t g_cpu_last_drawn = 0;

static const RoviGaugeStage kBatteryVoltageStages[] = {
  {126, lv_palette_main(LV_PALETTE_GREEN)},  // Fully charged (max safe): 12.60V
  {111, lv_palette_main(LV_PALETTE_GREEN)},  // Nominal / mid-charge: 11.10V
  {110, lv_palette_main(LV_PALETTE_AMBER)},  // Recharge soon (~20% left): ~11.0V
  {105, lv_palette_main(LV_PALETTE_ORANGE)}, // Low but safe: 10.50V
  {90, lv_palette_main(LV_PALETTE_RED)},     // Critical: 9.00V
};

static void rovi_voltage_apply_stale(void) {
  if (g_voltage_arc == nullptr || g_voltage_value_label == nullptr) {
    return;
  }

  lv_arc_set_value(g_voltage_arc, kVoltageMinX10);
  lv_obj_set_style_arc_color(g_voltage_arc, lv_color_hex(0x475569), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_voltage_value_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_label_set_text(g_voltage_value_label, "--");
}

static void rovi_cpu_apply_stale(void) {
  if (g_cpu_arc == nullptr || g_cpu_value_label == nullptr) {
    return;
  }

  lv_arc_set_value(g_cpu_arc, kCpuMin);
  lv_obj_set_style_arc_color(g_cpu_arc, lv_color_hex(0x475569), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_cpu_value_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_label_set_text(g_cpu_value_label, "--");
}

static void rovi_voltage_apply_fresh(int32_t voltage_x10) {
  if (g_voltage_arc == nullptr || g_voltage_value_label == nullptr) {
    return;
  }

  if (voltage_x10 < kVoltageMinX10) {
    voltage_x10 = kVoltageMinX10;
  } else if (voltage_x10 > kVoltageMaxX10) {
    voltage_x10 = kVoltageMaxX10;
  }

  lv_arc_set_value(g_voltage_arc, voltage_x10);
  lv_color_t color = rovi_pick_stage_color(voltage_x10, kBatteryVoltageStages,
                                           sizeof(kBatteryVoltageStages) / sizeof(kBatteryVoltageStages[0]),
                                           lv_palette_main(LV_PALETTE_GREEN));
  lv_obj_set_style_arc_color(g_voltage_arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_voltage_value_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);

  char buf[16];
  rovi_format_voltage(buf, sizeof(buf), voltage_x10);
  lv_label_set_text(g_voltage_value_label, buf);
}

static void rovi_cpu_apply_fresh(int32_t cpu_percent) {
  if (g_cpu_arc == nullptr || g_cpu_value_label == nullptr) {
    return;
  }

  if (cpu_percent < kCpuMin) {
    cpu_percent = kCpuMin;
  } else if (cpu_percent > kCpuMax) {
    cpu_percent = kCpuMax;
  }

  lv_arc_set_value(g_cpu_arc, cpu_percent);
  lv_obj_set_style_arc_color(g_cpu_arc, lv_palette_main(LV_PALETTE_AMBER), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_cpu_value_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);

  char buf[16];
  snprintf(buf, sizeof(buf), "%ld%%", static_cast<long>(cpu_percent));
  lv_label_set_text(g_cpu_value_label, buf);
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
    g_voltage_value_x10 = voltage_values_x10[voltage_idx];
    g_voltage_last_update_ms = now;
    g_voltage_has_value = true;
    last_voltage_publish_ms = now;
  }

  if (now - last_cpu_publish_ms >= 7000) {
    cpu_idx = (cpu_idx + 1) % (sizeof(cpu_values) / sizeof(cpu_values[0]));
    g_cpu_value = cpu_values[cpu_idx];
    g_cpu_last_update_ms = now;
    g_cpu_has_value = true;
    last_cpu_publish_ms = now;
  }

  bool voltage_stale = !g_voltage_has_value || (now - g_voltage_last_update_ms > kValueStaleTimeoutMs);
  if (voltage_stale) {
    if (!g_voltage_is_stale) {
      g_voltage_is_stale = true;
      rovi_voltage_apply_stale();
    }
  } else {
    if (g_voltage_is_stale || g_voltage_last_drawn_x10 != g_voltage_value_x10) {
      g_voltage_is_stale = false;
      g_voltage_last_drawn_x10 = g_voltage_value_x10;
      rovi_voltage_apply_fresh(g_voltage_value_x10);
    }
  }

  bool cpu_stale = !g_cpu_has_value || (now - g_cpu_last_update_ms > kValueStaleTimeoutMs);
  if (cpu_stale) {
    if (!g_cpu_is_stale) {
      g_cpu_is_stale = true;
      rovi_cpu_apply_stale();
    }
  } else {
    if (g_cpu_is_stale || g_cpu_last_drawn != g_cpu_value) {
      g_cpu_is_stale = false;
      g_cpu_last_drawn = g_cpu_value;
      rovi_cpu_apply_fresh(g_cpu_value);
    }
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

static void rovi_show_splash_from_sd(void) {
  static const char *kSplashPath = "S:rovi.png";

  lv_img_header_t hdr;
  if (lv_img_decoder_get_info(kSplashPath, &hdr) != LV_RES_OK) {
    Serial.printf("Splash image not found/decodable: %s\n", kSplashPath);
    return;
  }

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *img = lv_img_create(scr);
  lv_img_set_src(img, kSplashPath);

  bool rotate_90 = (hdr.w > hdr.h) && (screenHeight > screenWidth);
  if (rotate_90) {
    lv_img_set_pivot(img, hdr.w / 2, hdr.h / 2);
    lv_img_set_angle(img, 900);
  }

  uint32_t disp_w = rotate_90 ? hdr.h : hdr.w;
  uint32_t disp_h = rotate_90 ? hdr.w : hdr.h;
  uint32_t zoom_w = (screenWidth * 256U) / disp_w;
  uint32_t zoom_h = (screenHeight * 256U) / disp_h;
  uint32_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
  if (zoom > 256U) {
    zoom = 256U;
  }
  lv_img_set_zoom(img, zoom);

  lv_obj_center(img);

  uint32_t start = millis();
  while (millis() - start < 5000) {
    lv_timer_handler();
    delay(10);
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
  lv_obj_t *mbox = lv_msgbox_create(nullptr, "ROVI", action, btns, true);
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

static lv_obj_t *rovi_create_tile(lv_obj_t *parent) {
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_style_bg_color(tile, lv_color_hex(0x111827), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(tile, lv_color_hex(0x0B1220), LV_PART_MAIN);
  lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tile, 10, LV_PART_MAIN);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  return tile;
}

static lv_obj_t *rovi_create_arc_gauge_tile(lv_obj_t *parent,
                                           const char *title,
                                           int32_t min_value,
                                           int32_t max_value,
                                           int32_t value,
                                           const char *value_text,
                                           const char *min_label,
                                           const char *max_label,
                                           lv_color_t accent_color,
                                           lv_obj_t **out_arc,
                                           lv_obj_t **out_value_label) {
  lv_obj_t *tile = rovi_create_tile(parent);

  lv_obj_t *title_label = lv_label_create(tile);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *arc = lv_arc_create(tile);
  lv_obj_set_size(arc, 120, 120);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, min_value, max_value);
  lv_arc_set_value(arc, value);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x334155), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, accent_color, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *value_label = lv_label_create(tile);
  lv_obj_set_style_text_color(value_label, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(value_label, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(value_label, value_text);
  lv_obj_set_width(value_label, LV_PCT(100));
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(value_label, LV_ALIGN_CENTER, 0, 14);

  if (out_arc != nullptr) {
    *out_arc = arc;
  }
  if (out_value_label != nullptr) {
    *out_value_label = value_label;
  }

  if (min_label != nullptr && max_label != nullptr) {
    lv_obj_t *min_value_label = lv_label_create(tile);
    lv_label_set_text(min_value_label, min_label);
    lv_obj_set_style_text_color(min_value_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(min_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(min_value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *max_value_label = lv_label_create(tile);
    lv_label_set_text(max_value_label, max_label);
    lv_obj_set_style_text_color(max_value_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(max_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(max_value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  }

  return tile;
}

static lv_obj_t *rovi_create_multistage_arc_gauge_tile(lv_obj_t *parent,
                                                      const char *title,
                                                      int32_t min_value,
                                                      int32_t max_value,
                                                      int32_t value,
                                                      const char *value_text,
                                                      const char *min_label,
                                                      const char *max_label,
                                                      const RoviGaugeStage *stages,
                                                      size_t stage_count,
                                                      lv_obj_t **out_arc,
                                                      lv_obj_t **out_value_label) {
  lv_color_t color = rovi_pick_stage_color(value, stages, stage_count, lv_palette_main(LV_PALETTE_GREEN));
  return rovi_create_arc_gauge_tile(parent, title, min_value, max_value, value, value_text, min_label, max_label, color, out_arc,
                                   out_value_label);
}

static void rovi_dashboard_create(void) {
  lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(),
                                           lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_RED),
                                           true,
                                           LV_FONT_DEFAULT);
  lv_disp_set_theme(lv_disp_get_default(), theme);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1220), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  static lv_coord_t col_dsc[] = {0, 0, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {0, 0, 0, LV_GRID_TEMPLATE_LAST};
  col_dsc[0] = static_cast<lv_coord_t>(screenWidth / 2);
  col_dsc[1] = static_cast<lv_coord_t>(screenWidth / 2);
  row_dsc[0] = static_cast<lv_coord_t>(screenHeight / 3);
  row_dsc[1] = static_cast<lv_coord_t>(screenHeight / 3);
  row_dsc[2] = static_cast<lv_coord_t>(screenHeight / 3);

  lv_obj_t *grid = lv_obj_create(scr);
  lv_obj_set_size(grid, screenWidth, screenHeight);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(grid, 0, LV_PART_MAIN);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

  char voltage_buf[16];
  rovi_format_voltage(voltage_buf, sizeof(voltage_buf), kVoltageValueX10);

  lv_obj_t *tile_voltage =
    rovi_create_multistage_arc_gauge_tile(grid, "Voltage", kVoltageMinX10, kVoltageMaxX10, kVoltageValueX10, voltage_buf, "9V",
                                          "13V", kBatteryVoltageStages,
                                          sizeof(kBatteryVoltageStages) / sizeof(kBatteryVoltageStages[0]), &g_voltage_arc,
                                          &g_voltage_value_label);
  lv_obj_set_grid_cell(tile_voltage, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  char cpu_buf[16];
  snprintf(cpu_buf, sizeof(cpu_buf), "%ld%%", static_cast<long>(kCpuValue));
  lv_obj_t *tile_cpu =
    rovi_create_arc_gauge_tile(grid, "CPU", kCpuMin, kCpuMax, kCpuValue, cpu_buf, "0%", "100%",
                               lv_palette_main(LV_PALETTE_AMBER), &g_cpu_arc, &g_cpu_value_label);
  lv_obj_set_grid_cell(tile_cpu, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_t *tile_shutdown = rovi_create_tile(grid);
  lv_obj_set_grid_cell(tile_shutdown, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t *shutdown_title = lv_label_create(tile_shutdown);
  lv_label_set_text(shutdown_title, "Power");
  lv_obj_set_style_text_color(shutdown_title, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(shutdown_title, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(shutdown_title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *btn_shutdown = lv_btn_create(tile_shutdown);
  lv_obj_set_size(btn_shutdown, LV_PCT(100), 95);
  lv_obj_align(btn_shutdown, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_shutdown, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(btn_shutdown, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(btn_shutdown, 12, LV_PART_MAIN);
  static char shutdown_msg[] = "Shutdown requested (demo only)";
  lv_obj_add_event_cb(btn_shutdown, rovi_power_btn_event_cb, LV_EVENT_CLICKED, shutdown_msg);
  lv_obj_t *lbl_shutdown = lv_label_create(btn_shutdown);
  lv_label_set_text(lbl_shutdown, "Shutdown");
  lv_obj_center(lbl_shutdown);

  lv_obj_t *tile_restart = rovi_create_tile(grid);
  lv_obj_set_grid_cell(tile_restart, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t *restart_title = lv_label_create(tile_restart);
  lv_label_set_text(restart_title, "System");
  lv_obj_set_style_text_color(restart_title, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(restart_title, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(restart_title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *btn_restart = lv_btn_create(tile_restart);
  lv_obj_set_size(btn_restart, LV_PCT(100), 95);
  lv_obj_align(btn_restart, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_restart, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(btn_restart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(btn_restart, 12, LV_PART_MAIN);
  static char restart_msg[] = "Restart requested (demo only)";
  lv_obj_add_event_cb(btn_restart, rovi_power_btn_event_cb, LV_EVENT_CLICKED, restart_msg);
  lv_obj_t *lbl_restart = lv_label_create(btn_restart);
  lv_label_set_text(lbl_restart, "Restart");
  lv_obj_center(lbl_restart);

  lv_obj_t *tile_info = rovi_create_tile(grid);
  lv_obj_set_grid_cell(tile_info, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_t *lbl_rovi = lv_label_create(tile_info);
  lv_label_set_text(lbl_rovi, "ROVI");
  lv_obj_set_style_text_color(lbl_rovi, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_rovi, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(lbl_rovi, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lbl_demo = lv_label_create(tile_info);
  lv_label_set_text(lbl_demo, "Dashboard (demo timer)");
  lv_obj_set_style_text_color(lbl_demo, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_demo, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align_to(lbl_demo, lbl_rovi, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lv_obj_t *lbl_hint = lv_label_create(tile_info);
  lv_label_set_text(lbl_hint, "Demo timer updates\nNo live ROS data yet");
  lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *tile_ros = rovi_create_tile(grid);
  lv_obj_set_grid_cell(tile_ros, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_t *lbl_ros = lv_label_create(tile_ros);
  lv_label_set_text(lbl_ros, "ROS");
  lv_obj_set_style_text_color(lbl_ros, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_ros, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(lbl_ros, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lbl_status = lv_label_create(tile_ros);
  lv_label_set_text(lbl_status, "Status: demo/offline");
  lv_obj_set_style_text_color(lbl_status, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align_to(lbl_status, lbl_ros, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

  lv_obj_t *lbl_note = lv_label_create(tile_ros);
  lv_label_set_text(lbl_note, "Next: subscribe topics\nand drive gauges");
  lv_obj_set_style_text_color(lbl_note, lv_color_hex(0x94A3B8), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_note, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  uint32_t now = millis();
  g_voltage_value_x10 = kVoltageValueX10;
  g_voltage_last_update_ms = now;
  g_voltage_has_value = true;
  g_voltage_is_stale = false;
  g_voltage_last_drawn_x10 = g_voltage_value_x10;

  g_cpu_value = kCpuValue;
  g_cpu_last_update_ms = now;
  g_cpu_has_value = true;
  g_cpu_is_stale = false;
  g_cpu_last_drawn = g_cpu_value;

  lv_timer_create(rovi_demo_timer_cb, 200, nullptr);
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

  if (sd_ready) {
    rovi_show_splash_from_sd();
  }

  rovi_dashboard_create();

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  delay(1);
}
