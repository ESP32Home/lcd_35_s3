/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>

#include <LiveDashboard.h>
#include <WsLcd35S3Hal.h>

static constexpr const char *kConfigPath = "/config.json";

static ws_lcd_35_s3_hal::WsLcd35S3Hal g_hal;
static live_dashboard::LiveDashboard g_dashboard;
static bool g_dashboard_ready = false;

static void rovi_action_cb(const char *action_id, void *) {
  Serial.printf("ROVI action requested: %s\n", action_id != nullptr ? action_id : "(null)");
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

void setup() {
  Serial.begin(115200);
  Serial.println("ROVI dashboard (config-driven) example");

  if (!g_hal.begin()) {
    Serial.println("FATAL: HAL bring-up failed");
    while (true) {
      delay(1000);
    }
  }

  if (!g_dashboard.begin(g_hal.flashFs(), kConfigPath, g_hal.width(), g_hal.height(), g_hal.lvglFlashDriveLetter())) {
    Serial.println("Setup done (config error)");
    return;
  }
  g_dashboard_ready = true;

  Serial.printf("Config loaded: robot=%s\n", g_dashboard.robotName());

  g_dashboard.onAction("shutdown", rovi_action_cb, nullptr);
  g_dashboard.onAction("restart", rovi_action_cb, nullptr);

  Serial.println("Setup done");
}

void loop() {
  g_hal.loop();

  if (!g_dashboard_ready) {
    return;
  }

  g_dashboard.tick();

  const uint32_t now = millis();

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
    g_dashboard.publishGauge("voltage", voltage_values_x10[voltage_idx], voltage_buf);
    last_voltage_publish_ms = now;
  }

  if (now - last_cpu_publish_ms >= 7000) {
    cpu_idx = (cpu_idx + 1) % (sizeof(cpu_values) / sizeof(cpu_values[0]));
    char cpu_buf[16];
    snprintf(cpu_buf, sizeof(cpu_buf), "%ld%%", static_cast<long>(cpu_values[cpu_idx]));
    g_dashboard.publishGauge("cpu", cpu_values[cpu_idx], cpu_buf);
    last_cpu_publish_ms = now;
  }
}
