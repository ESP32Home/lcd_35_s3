/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>

#include <LiveDashboard.h>
#include <WsLcd35S3Hal.h>

#ifndef ROVI_ENABLE_JSONL_DEMO_REPLAY
#define ROVI_ENABLE_JSONL_DEMO_REPLAY 0
#endif

static constexpr const char *kConfigPath = "/config.json";

static ws_lcd_35_s3_hal::WsLcd35S3Hal g_hal;
static live_dashboard::LiveDashboard g_dashboard;
static bool g_dashboard_ready = false;

static void rovi_action_cb(const char *action_id, void *) {
  Serial.printf("ROVI action requested: %s\n", action_id != nullptr ? action_id : "(null)");
}

static void poll_event_lines_from_serial() {
  static char rx[1024 + 1]{};
  static size_t rx_len = 0;
  static bool rx_drop = false;

  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) {
      break;
    }

    if (c == '\n') {
      if (!rx_drop) {
        rx[rx_len] = '\0';
        if (rx_len > 0) {
          g_dashboard.ingestLine(rx);
        }
      }
      rx_len = 0;
      rx_drop = false;
      continue;
    }

    if (c == '\r') {
      continue;
    }

    if (rx_drop) {
      continue;
    }

    if (rx_len < 1024) {
      rx[rx_len++] = static_cast<char>(c);
    } else {
      Serial.println("EVENT: RX line too long (max 1024), dropping");
      rx_len = 0;
      rx_drop = true;
    }
  }
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

  live_dashboard::LiveDashboardOptions options{};
  options.demo_replay = (ROVI_ENABLE_JSONL_DEMO_REPLAY != 0);
  options.demo_path = "/test.jsonl";
  options.demo_period_ms = 1000;

  if (!g_dashboard.begin(g_hal.flashFs(),
                         kConfigPath,
                         g_hal.width(),
                         g_hal.height(),
                         g_hal.lvglFlashDriveLetter(),
                         options)) {
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
  if (!g_dashboard_ready) {
    g_hal.loop();
    return;
  }

  g_dashboard.tick();
  poll_event_lines_from_serial();

  g_hal.loop();
}
