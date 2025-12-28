/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include <Arduino.h>

#include <ArduinoJson.h>
#include <FS.h>
#include <LiveDashboard.h>
#include <WsLcd35S3Hal.h>

#ifndef ROVI_ENABLE_DEMO
#define ROVI_ENABLE_DEMO 1
#endif

static constexpr const char *kConfigPath = "/config.json";
static constexpr const char *kDemoPath = "/test.jsonl";

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

static void demo_apply_json(JsonVariant v) {
  if (v.is<JsonArray>()) {
    for (JsonVariant item : v.as<JsonArray>()) {
      demo_apply_json(item);
    }
    return;
  }

  JsonObject obj = v.as<JsonObject>();
  if (obj.isNull()) {
    return;
  }

  const char *id = obj["id"];
  const char *text = obj["text"];
  if (id == nullptr || text == nullptr) {
    Serial.println("DEMO: missing id/text");
    return;
  }

  if (!obj["value"].is<int32_t>()) {
    Serial.println("DEMO: missing/invalid value");
    return;
  }
  const int32_t value = obj["value"].as<int32_t>();

  if (!g_dashboard.publishGauge(id, value, text)) {
    Serial.printf("DEMO: unknown gauge id: %s\n", id);
  }
}

static void demo_apply_line(const char *line) {
  if (line == nullptr) {
    return;
  }

  while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
    ++line;
  }
  if (*line == '\0') {
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("DEMO: JSON parse error: %s\n", err.c_str());
    return;
  }

  demo_apply_json(doc.as<JsonVariant>());
}

static bool demo_read_line(File &f, char *out, size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return false;
  }
  out[0] = '\0';

  if (!f) {
    return false;
  }

  size_t idx = 0;
  bool got_any = false;
  while (f.available()) {
    int c = f.read();
    if (c < 0) {
      break;
    }
    got_any = true;
    if (c == '\n') {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (idx + 1 < out_size) {
      out[idx++] = static_cast<char>(c);
    } else {
      while (f.available()) {
        int d = f.read();
        if (d < 0 || d == '\n') {
          break;
        }
      }
      break;
    }
  }

  out[idx] = '\0';
  return got_any;
}

static void demo_poll_serial() {
  static char rx[256];
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
          demo_apply_line(rx);
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

    if (rx_len + 1 < sizeof(rx)) {
      rx[rx_len++] = static_cast<char>(c);
    } else {
      Serial.println("DEMO: RX line too long, dropping");
      rx_len = 0;
      rx_drop = true;
    }
  }
}

static void demo() {
  demo_poll_serial();

  static File file;
  static uint32_t last_line_ms = 0;
  static uint32_t next_open_try_ms = 0;

  const uint32_t now = millis();
  if (now - last_line_ms < 1000) {
    return;
  }
  last_line_ms = now;

  if (!file) {
    if (now < next_open_try_ms) {
      return;
    }
    next_open_try_ms = now + 5000;
    file = g_hal.flashFs().open(kDemoPath, "r");
    if (!file) {
      Serial.printf("DEMO: missing %s (uploadfs)\n", kDemoPath);
      return;
    }
  }

  char line[256];
  for (int attempts = 0; attempts < 8; ++attempts) {
    if (!demo_read_line(file, line, sizeof(line))) {
      file.seek(0);
      if (!demo_read_line(file, line, sizeof(line))) {
        return;
      }
    }
    if (line[0] == '\0') {
      continue;
    }
    demo_apply_line(line);
    break;
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

#if ROVI_ENABLE_DEMO
  demo();
#endif
}
