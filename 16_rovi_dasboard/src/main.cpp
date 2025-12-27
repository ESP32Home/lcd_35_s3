/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>
#include <lvgl.h>

#include <LiveDashboard.h>

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

static constexpr uint32_t kSplashDurationMs = 3000;
static const char *kSplashLvglPath = "S:rovi.png";

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

static const live_dashboard::Stage kBatteryVoltageStages[] = {
  {126, lv_palette_main(LV_PALETTE_GREEN)},  // Fully charged (max safe): 12.60V
  {111, lv_palette_main(LV_PALETTE_GREEN)},  // Nominal / mid-charge: 11.10V
  {110, lv_palette_main(LV_PALETTE_AMBER)},  // Recharge soon (~20% left): ~11.0V
  {105, lv_palette_main(LV_PALETTE_ORANGE)}, // Low but safe: 10.50V
  {90, lv_palette_main(LV_PALETTE_RED)},     // Critical: 9.00V
};

static live_dashboard::LiveDashboard g_dashboard;

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
    rovi_show_splash_from_sd(kSplashLvglPath, kSplashDurationMs);
  }

  char voltage_buf[16];
  rovi_format_voltage(voltage_buf, sizeof(voltage_buf), kVoltageValueX10);

  char cpu_buf[16];
  snprintf(cpu_buf, sizeof(cpu_buf), "%ld%%", static_cast<long>(kCpuValue));

  live_dashboard::LiveDashboardConfig dash_cfg{};
  dash_cfg.screen_width = static_cast<lv_coord_t>(screenWidth);
  dash_cfg.screen_height = static_cast<lv_coord_t>(screenHeight);
  dash_cfg.stale_timeout_ms = kValueStaleTimeoutMs;

  dash_cfg.voltage_gauge.title = "Voltage";
  dash_cfg.voltage_gauge.min_value = kVoltageMinX10;
  dash_cfg.voltage_gauge.max_value = kVoltageMaxX10;
  dash_cfg.voltage_gauge.initial_value = kVoltageValueX10;
  dash_cfg.voltage_gauge.initial_text = voltage_buf;
  dash_cfg.voltage_gauge.min_label = "9V";
  dash_cfg.voltage_gauge.max_label = "13V";
  dash_cfg.voltage_gauge.stages = kBatteryVoltageStages;
  dash_cfg.voltage_gauge.stage_count = sizeof(kBatteryVoltageStages) / sizeof(kBatteryVoltageStages[0]);
  dash_cfg.voltage_gauge.stages_fallback_color = lv_palette_main(LV_PALETTE_GREEN);

  dash_cfg.cpu_gauge.title = "CPU";
  dash_cfg.cpu_gauge.min_value = kCpuMin;
  dash_cfg.cpu_gauge.max_value = kCpuMax;
  dash_cfg.cpu_gauge.initial_value = kCpuValue;
  dash_cfg.cpu_gauge.initial_text = cpu_buf;
  dash_cfg.cpu_gauge.min_label = "0%";
  dash_cfg.cpu_gauge.max_label = "100%";
  dash_cfg.cpu_gauge.accent_color = lv_palette_main(LV_PALETTE_AMBER);

  dash_cfg.shutdown_button.tile_title = "Power";
  dash_cfg.shutdown_button.button_label = "Shutdown";
  dash_cfg.shutdown_button.button_color = lv_palette_main(LV_PALETTE_RED);

  dash_cfg.restart_button.tile_title = "System";
  dash_cfg.restart_button.button_label = "Restart";
  dash_cfg.restart_button.button_color = lv_palette_main(LV_PALETTE_BLUE);

  dash_cfg.info_tile.title = "ROVI";
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
