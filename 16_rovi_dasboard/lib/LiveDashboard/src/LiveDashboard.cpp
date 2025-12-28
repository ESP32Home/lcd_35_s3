#include "LiveDashboard.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace live_dashboard {
namespace {

static const lv_color_t kTileBg = lv_color_hex(0x111827);
static const lv_color_t kTileBorder = lv_color_hex(0x0B1220);
static const lv_color_t kTextPrimary = lv_color_hex(0xE2E8F0);
static const lv_color_t kTextSecondary = lv_color_hex(0x94A3B8);
static const lv_color_t kArcBg = lv_color_hex(0x334155);
static const lv_color_t kStaleArc = lv_color_hex(0x475569);

static constexpr size_t kMaxStagesPerGauge = 8;

struct Stage {
  int32_t threshold;
  lv_color_t color;
};

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
  if (dst == nullptr || dst_size == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_size, "%s", src);
}

static int stricmp_(const char *a, const char *b) {
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
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return static_cast<unsigned char>(ca) - static_cast<unsigned char>(cb);
    ++a;
    ++b;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

static bool parse_lv_color_(const char *value, lv_color_t *out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }

  if (value[0] == '#') {
    char *end = nullptr;
    uint32_t rgb = strtoul(value + 1, &end, 16);
    if (end == value + 1) return false;
    *out = lv_color_hex(rgb);
    return true;
  }

  if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    char *end = nullptr;
    uint32_t rgb = strtoul(value + 2, &end, 16);
    if (end == value + 2) return false;
    *out = lv_color_hex(rgb);
    return true;
  }

  if (stricmp_(value, "green") == 0) {
    *out = lv_palette_main(LV_PALETTE_GREEN);
    return true;
  }
  if (stricmp_(value, "amber") == 0) {
    *out = lv_palette_main(LV_PALETTE_AMBER);
    return true;
  }
  if (stricmp_(value, "orange") == 0) {
    *out = lv_palette_main(LV_PALETTE_ORANGE);
    return true;
  }
  if (stricmp_(value, "red") == 0) {
    *out = lv_palette_main(LV_PALETTE_RED);
    return true;
  }
  if (stricmp_(value, "blue") == 0) {
    *out = lv_palette_main(LV_PALETTE_BLUE);
    return true;
  }
  if (stricmp_(value, "cyan") == 0) {
    *out = lv_palette_main(LV_PALETTE_CYAN);
    return true;
  }
  if (stricmp_(value, "purple") == 0) {
    *out = lv_palette_main(LV_PALETTE_PURPLE);
    return true;
  }
  if (stricmp_(value, "teal") == 0) {
    *out = lv_palette_main(LV_PALETTE_TEAL);
    return true;
  }
  if (stricmp_(value, "yellow") == 0) {
    *out = lv_palette_main(LV_PALETTE_YELLOW);
    return true;
  }
  if (stricmp_(value, "grey") == 0 || stricmp_(value, "gray") == 0) {
    *out = lv_palette_main(LV_PALETTE_GREY);
    return true;
  }
  if (stricmp_(value, "white") == 0) {
    *out = lv_color_white();
    return true;
  }

  return false;
}

static void sort_stages_desc_(Stage *stages, size_t stage_count) {
  if (stages == nullptr || stage_count < 2) {
    return;
  }
  for (size_t i = 0; i < stage_count; ++i) {
    for (size_t j = 0; j + 1 < stage_count; ++j) {
      if (stages[j].threshold < stages[j + 1].threshold) {
        Stage tmp = stages[j];
        stages[j] = stages[j + 1];
        stages[j + 1] = tmp;
      }
    }
  }
}

static lv_color_t pick_stage_color_(int32_t value, const Stage *stages, size_t stage_count, lv_color_t fallback) {
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

static lv_obj_t *create_tile_(lv_obj_t *parent) {
  lv_obj_t *tile = lv_obj_create(parent);
  lv_obj_set_style_bg_color(tile, kTileBg, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(tile, kTileBorder, LV_PART_MAIN);
  lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tile, 10, LV_PART_MAIN);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  return tile;
}

static void show_config_error_screen_(const char *fatal_message) {
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

static bool show_splash_from_lvgl_path_(const char *lvgl_path,
                                       lv_coord_t screen_width,
                                       lv_coord_t screen_height,
                                       uint32_t duration_ms,
                                       lv_color_t background_color) {
  if (lvgl_path == nullptr || lvgl_path[0] == '\0') {
    return false;
  }

  lv_img_header_t hdr;
  if (lv_img_decoder_get_info(lvgl_path, &hdr) != LV_RES_OK) {
    return false;
  }

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, background_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *img = lv_img_create(scr);
  lv_img_set_src(img, lvgl_path);

  bool rotate_90 = (hdr.w > hdr.h) && (screen_height > screen_width);
  if (rotate_90) {
    lv_img_set_pivot(img, hdr.w / 2, hdr.h / 2);
    lv_img_set_angle(img, 900);
  }

  uint32_t disp_w = rotate_90 ? hdr.h : hdr.w;
  uint32_t disp_h = rotate_90 ? hdr.w : hdr.h;
  if (disp_w == 0 || disp_h == 0) {
    return false;
  }

  uint32_t zoom_w = (static_cast<uint32_t>(screen_width) * 256U) / disp_w;
  uint32_t zoom_h = (static_cast<uint32_t>(screen_height) * 256U) / disp_h;
  uint32_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
  if (zoom > 256U) zoom = 256U;
  lv_img_set_zoom(img, zoom);
  lv_obj_center(img);

  if (duration_ms == 0) {
    return true;
  }

  uint32_t start = millis();
  while (millis() - start < duration_ms) {
    lv_timer_handler();
    delay(10);
  }
  return true;
}

class ArcGauge {
public:
  void create(lv_obj_t *tile,
              const char *title,
              int32_t min_value,
              int32_t max_value,
              int32_t initial_value,
              const char *initial_text,
              const char *min_label,
              const char *max_label,
              lv_color_t accent_color,
              const Stage *stages,
              size_t stage_count,
              uint32_t stale_timeout_ms,
              const char *stale_text) {
    tile_ = tile;
    min_value_ = min_value;
    max_value_ = max_value;
    accent_color_ = accent_color;
    stages_ = stages;
    stage_count_ = stage_count;
    stale_timeout_ms_ = stale_timeout_ms;
    stale_text_ = (stale_text != nullptr && stale_text[0] != '\0') ? stale_text : "--";

    lv_obj_t *title_label = lv_label_create(tile_);
    lv_label_set_text(title_label, title != nullptr ? title : "");
    lv_obj_set_style_text_color(title_label, kTextPrimary, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    arc_ = lv_arc_create(tile_);
    lv_obj_set_size(arc_, 120, 120);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_arc_set_range(arc_, min_value_, max_value_);
    lv_obj_set_style_arc_width(arc_, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, kArcBg, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc_, 0, LV_PART_MAIN);
    lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(arc_, LV_ALIGN_CENTER, 0, 8);

    value_label_ = lv_label_create(tile_);
    lv_obj_set_style_text_color(value_label_, kTextPrimary, LV_PART_MAIN);
    lv_obj_set_style_text_font(value_label_, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_width(value_label_, LV_PCT(100));
    lv_obj_set_style_text_align(value_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(value_label_, initial_text != nullptr ? initial_text : "");
    lv_obj_align_to(value_label_, arc_, LV_ALIGN_CENTER, 2, 8);

    if (min_label != nullptr && max_label != nullptr) {
      lv_obj_t *min_value_label = lv_label_create(tile_);
      lv_label_set_text(min_value_label, min_label);
      lv_obj_set_style_text_color(min_value_label, kTextSecondary, LV_PART_MAIN);
      lv_obj_set_style_text_font(min_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
      lv_obj_align(min_value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

      lv_obj_t *max_value_label = lv_label_create(tile_);
      lv_label_set_text(max_value_label, max_label);
      lv_obj_set_style_text_color(max_value_label, kTextSecondary, LV_PART_MAIN);
      lv_obj_set_style_text_font(max_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
      lv_obj_align(max_value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    publish(initial_value, initial_text, millis());
  }

  void publish(int32_t value, const char *value_text, uint32_t now_ms) {
    value_ = value;
    last_update_ms_ = now_ms;
    has_value_ = true;

    is_stale_ = false;
    applyFresh_(value_, value_text);
  }

  void tick(uint32_t now_ms) {
    if (arc_ == nullptr || value_label_ == nullptr) {
      return;
    }
    bool stale = !has_value_ || (stale_timeout_ms_ > 0 && (now_ms - last_update_ms_ > stale_timeout_ms_));
    if (stale && !is_stale_) {
      is_stale_ = true;
      applyStale_();
    }
  }

private:
  lv_color_t indicatorColorForValue_(int32_t value) const {
    if (stages_ != nullptr && stage_count_ > 0) {
      return pick_stage_color_(value, stages_, stage_count_, accent_color_);
    }
    return accent_color_;
  }

  void applyFresh_(int32_t value, const char *value_text) {
    if (value < min_value_) value = min_value_;
    if (value > max_value_) value = max_value_;

    lv_arc_set_value(arc_, value);
    lv_obj_set_style_arc_color(arc_, indicatorColorForValue_(value), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(value_label_, kTextPrimary, LV_PART_MAIN);
    lv_label_set_text(value_label_, value_text != nullptr ? value_text : "");
  }

  void applyStale_() {
    lv_arc_set_value(arc_, min_value_);
    lv_obj_set_style_arc_color(arc_, kStaleArc, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(value_label_, kTextSecondary, LV_PART_MAIN);
    lv_label_set_text(value_label_, stale_text_);
  }

  lv_obj_t *tile_ = nullptr;
  lv_obj_t *arc_ = nullptr;
  lv_obj_t *value_label_ = nullptr;

  int32_t min_value_ = 0;
  int32_t max_value_ = 100;
  int32_t value_ = 0;
  bool has_value_ = false;
  bool is_stale_ = true;
  uint32_t last_update_ms_ = 0;
  uint32_t stale_timeout_ms_ = 0;
  const char *stale_text_ = "--";

  lv_color_t accent_color_ = lv_palette_main(LV_PALETTE_BLUE);
  const Stage *stages_ = nullptr;
  size_t stage_count_ = 0;
};

struct TileSlot {
  bool used = false;
  char id[LIVE_DASHBOARD_ID_MAX_LEN]{};
  lv_obj_t *obj = nullptr;
};

struct GaugeSlot {
  bool used = false;
  char id[LIVE_DASHBOARD_ID_MAX_LEN]{};
  ArcGauge gauge{};
  Stage stages[kMaxStagesPerGauge]{};
  size_t stage_count = 0;
};

struct ButtonSlot {
  bool used = false;
  char action_id[LIVE_DASHBOARD_ID_MAX_LEN]{};
  LiveDashboard::ActionCallback cb = nullptr;
  void *user = nullptr;
};

static void button_event_cb_(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }

  ButtonSlot *slot = static_cast<ButtonSlot *>(lv_event_get_user_data(e));
  if (slot == nullptr || slot->cb == nullptr) {
    return;
  }

  slot->cb(slot->action_id, slot->user);
}

} // namespace

class LiveDashboardImpl {
public:
  bool begin(LiveDashboard &api, fs::FS &fs, const char *config_path, uint16_t screen_width, uint16_t screen_height, char lvgl_drive_letter);
  void tick();

  bool publishGauge(const char *gauge_id, int32_t value, const char *text);
  bool onAction(const char *action_id, LiveDashboard::ActionCallback cb, void *user);
  const char *robotName() const { return robot_name_; }

private:
  lv_obj_t *find_tile_(const char *tile_id);
  GaugeSlot *find_gauge_(const char *gauge_id);

  bool load_and_build_(LiveDashboard &api, fs::FS &fs, const char *config_path);
  bool build_from_json_(LiveDashboard &api, JsonObject root);

  uint16_t screen_width_ = 0;
  uint16_t screen_height_ = 0;
  char lvgl_drive_letter_ = 'F';

  uint32_t stale_timeout_ms_ = 5000;
  lv_color_t background_color_ = lv_color_hex(0x0B1220);
  bool dark_theme_ = true;

  char robot_name_[32]{};
  char splash_path_[64]{};
  uint32_t splash_duration_ms_ = 0;

  TileSlot tiles_[LIVE_DASHBOARD_MAX_TILES]{};
  size_t tile_count_ = 0;

  GaugeSlot gauges_[LIVE_DASHBOARD_MAX_GAUGES]{};
  size_t gauge_count_ = 0;

  ButtonSlot buttons_[LIVE_DASHBOARD_MAX_BUTTONS]{};
  size_t button_count_ = 0;

  lv_obj_t *grid_ = nullptr;
  lv_coord_t col_dsc_[LIVE_DASHBOARD_MAX_TILES + 1]{};
  lv_coord_t row_dsc_[LIVE_DASHBOARD_MAX_TILES + 1]{};
};

static LiveDashboardImpl g_impl;

bool LiveDashboardImpl::begin(LiveDashboard &api,
                              fs::FS &fs,
                              const char *config_path,
                              uint16_t screen_width,
                              uint16_t screen_height,
                              char lvgl_drive_letter) {
  screen_width_ = screen_width;
  screen_height_ = screen_height;
  lvgl_drive_letter_ = lvgl_drive_letter;

  robot_name_[0] = '\0';
  splash_path_[0] = '\0';
  splash_duration_ms_ = 0;
  tile_count_ = 0;
  gauge_count_ = 0;
  button_count_ = 0;
  grid_ = nullptr;

  for (size_t i = 0; i < LIVE_DASHBOARD_MAX_TILES; ++i) {
    tiles_[i] = TileSlot{};
  }
  for (size_t i = 0; i < LIVE_DASHBOARD_MAX_GAUGES; ++i) {
    gauges_[i] = GaugeSlot{};
  }
  for (size_t i = 0; i < LIVE_DASHBOARD_MAX_BUTTONS; ++i) {
    buttons_[i] = ButtonSlot{};
  }

  return load_and_build_(api, fs, config_path);
}

void LiveDashboardImpl::tick() {
  uint32_t now = millis();
  for (size_t i = 0; i < gauge_count_; ++i) {
    if (gauges_[i].used) {
      gauges_[i].gauge.tick(now);
    }
  }
}

bool LiveDashboardImpl::publishGauge(const char *gauge_id, int32_t value, const char *text) {
  GaugeSlot *slot = find_gauge_(gauge_id);
  if (slot == nullptr) {
    return false;
  }
  slot->gauge.publish(value, text, millis());
  return true;
}

bool LiveDashboardImpl::onAction(const char *action_id, LiveDashboard::ActionCallback cb, void *user) {
  bool found = false;
  for (size_t i = 0; i < button_count_; ++i) {
    if (!buttons_[i].used) continue;
    if (strncmp(buttons_[i].action_id, action_id, sizeof(buttons_[i].action_id)) == 0) {
      buttons_[i].cb = cb;
      buttons_[i].user = user;
      found = true;
    }
  }
  return found;
}

lv_obj_t *LiveDashboardImpl::find_tile_(const char *tile_id) {
  if (tile_id == nullptr) return nullptr;
  for (size_t i = 0; i < tile_count_; ++i) {
    if (!tiles_[i].used) continue;
    if (strncmp(tiles_[i].id, tile_id, sizeof(tiles_[i].id)) == 0) {
      return tiles_[i].obj;
    }
  }
  return nullptr;
}

GaugeSlot *LiveDashboardImpl::find_gauge_(const char *gauge_id) {
  if (gauge_id == nullptr) return nullptr;
  for (size_t i = 0; i < gauge_count_; ++i) {
    if (!gauges_[i].used) continue;
    if (strncmp(gauges_[i].id, gauge_id, sizeof(gauges_[i].id)) == 0) {
      return &gauges_[i];
    }
  }
  return nullptr;
}

bool LiveDashboardImpl::load_and_build_(LiveDashboard &api, fs::FS &fs, const char *config_path) {
  if (config_path == nullptr || config_path[0] == '\0') {
    Serial.println("FATAL: config path not set");
    show_config_error_screen_("Config path not set");
    return false;
  }

  File f = fs.open(config_path, "r");
  if (!f) {
    Serial.printf("FATAL: config not found: %s\n", config_path);
    show_config_error_screen_("Config not found");
    return false;
  }

  static StaticJsonDocument<8192> doc;
  doc.clear();

  DeserializationError err = deserializeJson(doc, f);
  if (err) {
    Serial.printf("FATAL: config parse error: %s\n", err.c_str());
    show_config_error_screen_(err.c_str());
    return false;
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    Serial.println("FATAL: config root is not an object");
    show_config_error_screen_("Config root is not an object");
    return false;
  }

  return build_from_json_(api, root);
}

bool LiveDashboardImpl::build_from_json_(LiveDashboard &api, JsonObject root) {
  const char *robot_name = root["robot_name"];
  if (robot_name == nullptr) {
    show_config_error_screen_("Missing: robot_name");
    return false;
  }
  copy_cstr(robot_name_, sizeof(robot_name_), robot_name);

  JsonObject ui = root["ui"].as<JsonObject>();
  if (ui.isNull() || !ui["dark_theme"].is<bool>() || !ui["stale_timeout_ms"].is<uint32_t>()) {
    show_config_error_screen_("Missing/invalid: ui");
    return false;
  }
  dark_theme_ = ui["dark_theme"].as<bool>();
  stale_timeout_ms_ = ui["stale_timeout_ms"].as<uint32_t>();

  const char *bg_color = ui["background"];
  if (bg_color != nullptr) {
    parse_lv_color_(bg_color, &background_color_);
  }

  JsonObject splash = ui["splash"].as<JsonObject>();
  if (!splash.isNull()) {
    const char *path = splash["path"];
    if (path != nullptr) copy_cstr(splash_path_, sizeof(splash_path_), path);
    if (splash["duration_ms"].is<uint32_t>()) splash_duration_ms_ = splash["duration_ms"].as<uint32_t>();
  }

  JsonObject layout = root["layout"].as<JsonObject>();
  if (layout.isNull() || !layout["cols"].is<uint8_t>() || !layout["rows"].is<uint8_t>()) {
    show_config_error_screen_("Missing/invalid: layout.(cols/rows)");
    return false;
  }

  const uint8_t cols = layout["cols"].as<uint8_t>();
  const uint8_t rows = layout["rows"].as<uint8_t>();
  if (cols == 0 || rows == 0 || static_cast<size_t>(cols) * static_cast<size_t>(rows) > LIVE_DASHBOARD_MAX_TILES) {
    show_config_error_screen_("Invalid: layout cols/rows");
    return false;
  }

  JsonArray tiles = layout["tiles"].as<JsonArray>();
  if (tiles.isNull() || tiles.size() != static_cast<size_t>(cols) * static_cast<size_t>(rows)) {
    show_config_error_screen_("Invalid: layout.tiles size");
    return false;
  }

  lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(),
                                           lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_RED),
                                           dark_theme_,
                                           LV_FONT_DEFAULT);
  lv_disp_set_theme(lv_disp_get_default(), theme);

  if (splash_path_[0] != '\0' && splash_duration_ms_ > 0) {
    char lvgl_path[96];
    if (strchr(splash_path_, ':') != nullptr) {
      copy_cstr(lvgl_path, sizeof(lvgl_path), splash_path_);
    } else {
      snprintf(lvgl_path, sizeof(lvgl_path), "%c:%s", lvgl_drive_letter_, splash_path_);
    }
    if (!show_splash_from_lvgl_path_(lvgl_path, screen_width_, screen_height_, splash_duration_ms_, background_color_)) {
      Serial.printf("Splash skipped (not found/decodable): %s\n", lvgl_path);
    }
  }

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, background_color_, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  for (uint8_t c = 0; c < cols; ++c) col_dsc_[c] = LV_GRID_FR(1);
  col_dsc_[cols] = LV_GRID_TEMPLATE_LAST;

  for (uint8_t r = 0; r < rows; ++r) row_dsc_[r] = LV_GRID_FR(1);
  row_dsc_[rows] = LV_GRID_TEMPLATE_LAST;

  grid_ = lv_obj_create(scr);
  lv_obj_set_size(grid_, screen_width_, screen_height_);
  lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(grid_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(grid_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(grid_, 0, LV_PART_MAIN);
  lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid_, col_dsc_, row_dsc_);

  tile_count_ = 0;
  for (JsonVariant v : tiles) {
    if (tile_count_ >= LIVE_DASHBOARD_MAX_TILES) break;
    JsonObject tile_cfg = v.as<JsonObject>();
    const char *tile_id = tile_cfg["id"];
    if (tile_id == nullptr) {
      show_config_error_screen_("Missing: layout.tiles[].id");
      return false;
    }

    TileSlot &slot = tiles_[tile_count_];
    slot.used = true;
    copy_cstr(slot.id, sizeof(slot.id), tile_id);
    slot.obj = create_tile_(grid_);

    const uint8_t col = static_cast<uint8_t>(tile_count_ % cols);
    const uint8_t row = static_cast<uint8_t>(tile_count_ / cols);
    lv_obj_set_grid_cell(slot.obj, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

    ++tile_count_;
  }

  gauge_count_ = 0;
  JsonArray gauges = root["gauges"].as<JsonArray>();
  if (!gauges.isNull()) {
    if (gauges.size() > LIVE_DASHBOARD_MAX_GAUGES) {
      show_config_error_screen_("Too many gauges (LIVE_DASHBOARD_MAX_GAUGES)");
      return false;
    }
    for (JsonVariant v : gauges) {
      if (gauge_count_ >= LIVE_DASHBOARD_MAX_GAUGES) break;
      JsonObject g = v.as<JsonObject>();
      const char *id = g["id"];
      const char *tile_id = g["tile_id"];
      const char *title = g["title"];
      if (id == nullptr || tile_id == nullptr || title == nullptr) {
        show_config_error_screen_("Missing: gauges[].(id/tile_id/title)");
        return false;
      }

      lv_obj_t *tile = find_tile_(tile_id);
      if (tile == nullptr) {
        show_config_error_screen_("Invalid gauges[].tile_id");
        return false;
      }

      if (!g["min"].is<int32_t>() || !g["max"].is<int32_t>() || !g["initial"].is<int32_t>() || !g["initial_text"].is<const char *>()) {
        show_config_error_screen_("Missing/invalid: gauges[] range/initial");
        return false;
      }

      const int32_t min_value = g["min"].as<int32_t>();
      const int32_t max_value = g["max"].as<int32_t>();
      const int32_t initial_value = g["initial"].as<int32_t>();
      const char *initial_text = g["initial_text"].as<const char *>();
      const char *min_label = g["min_label"];
      const char *max_label = g["max_label"];
      const char *stale_text = g["stale_text"];

      lv_color_t accent = lv_palette_main(LV_PALETTE_BLUE);
      const char *accent_str = g["accent"];
      if (accent_str == nullptr || !parse_lv_color_(accent_str, &accent)) {
        show_config_error_screen_("Missing/invalid: gauges[].accent");
        return false;
      }

      GaugeSlot &slot = gauges_[gauge_count_];
      slot.used = true;
      copy_cstr(slot.id, sizeof(slot.id), id);

      slot.stage_count = 0;
      JsonArray stages = g["stages"].as<JsonArray>();
      if (!stages.isNull()) {
        for (JsonVariant stage_v : stages) {
          if (slot.stage_count >= kMaxStagesPerGauge) break;
          JsonObject stage = stage_v.as<JsonObject>();
          if (stage.isNull()) continue;

          int32_t threshold = 0;
          if (stage["t"].is<int32_t>()) threshold = stage["t"].as<int32_t>();
          else if (stage["threshold"].is<int32_t>()) threshold = stage["threshold"].as<int32_t>();
          else continue;

          const char *color_str = nullptr;
          if (stage["c"].is<const char *>()) color_str = stage["c"].as<const char *>();
          else if (stage["color"].is<const char *>()) color_str = stage["color"].as<const char *>();
          else continue;

          lv_color_t color;
          if (!parse_lv_color_(color_str, &color)) continue;

          slot.stages[slot.stage_count++] = Stage{threshold, color};
        }
        sort_stages_desc_(slot.stages, slot.stage_count);
      }

      slot.gauge.create(tile,
                        title,
                        min_value,
                        max_value,
                        initial_value,
                        initial_text,
                        min_label,
                        max_label,
                        accent,
                        slot.stage_count > 0 ? slot.stages : nullptr,
                        slot.stage_count,
                        stale_timeout_ms_,
                        stale_text);

      ++gauge_count_;
    }
  }

  button_count_ = 0;
  JsonArray buttons = root["buttons"].as<JsonArray>();
  if (!buttons.isNull()) {
    if (buttons.size() > LIVE_DASHBOARD_MAX_BUTTONS) {
      show_config_error_screen_("Too many buttons (LIVE_DASHBOARD_MAX_BUTTONS)");
      return false;
    }
    for (JsonVariant v : buttons) {
      if (button_count_ >= LIVE_DASHBOARD_MAX_BUTTONS) break;
      JsonObject b = v.as<JsonObject>();
      const char *tile_id = b["tile_id"];
      const char *tile_title = b["tile_title"];
      const char *label = b["label"];
      const char *color_str = b["color"];
      const char *action_id = b["action_id"];
      if (tile_id == nullptr || tile_title == nullptr || label == nullptr || color_str == nullptr || action_id == nullptr) {
        show_config_error_screen_("Missing: buttons[]");
        return false;
      }

      lv_obj_t *tile = find_tile_(tile_id);
      if (tile == nullptr) {
        show_config_error_screen_("Invalid buttons[].tile_id");
        return false;
      }

      lv_color_t color;
      if (!parse_lv_color_(color_str, &color)) {
        show_config_error_screen_("Invalid buttons[].color");
        return false;
      }

      lv_obj_t *title = lv_label_create(tile);
      lv_label_set_text(title, tile_title);
      lv_obj_set_style_text_color(title, kTextPrimary, LV_PART_MAIN);
      lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
      lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

      lv_coord_t height = 95;
      if (b["height"].is<uint16_t>()) height = static_cast<lv_coord_t>(b["height"].as<uint16_t>());

      lv_obj_t *btn = lv_btn_create(tile);
      lv_obj_set_size(btn, LV_PCT(100), height);
      lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_set_style_bg_color(btn, color, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);

      lv_obj_t *lbl = lv_label_create(btn);
      lv_label_set_text(lbl, label);
      lv_obj_center(lbl);

      ButtonSlot &slot = buttons_[button_count_];
      slot.used = true;
      copy_cstr(slot.action_id, sizeof(slot.action_id), action_id);
      slot.cb = nullptr;
      slot.user = nullptr;

      lv_obj_add_event_cb(btn, button_event_cb_, LV_EVENT_CLICKED, &slot);
      ++button_count_;
    }
  }

  JsonArray text_tiles = root["text_tiles"].as<JsonArray>();
  if (!text_tiles.isNull()) {
    for (JsonVariant v : text_tiles) {
      JsonObject t = v.as<JsonObject>();
      const char *tile_id = t["tile_id"];
      const char *title = t["title"];
      const char *subtitle = t["subtitle"];
      const char *body = t["body"];
      if (tile_id == nullptr || title == nullptr || body == nullptr) {
        show_config_error_screen_("Missing: text_tiles[]");
        return false;
      }

      lv_obj_t *tile = find_tile_(tile_id);
      if (tile == nullptr) {
        show_config_error_screen_("Invalid text_tiles[].tile_id");
        return false;
      }

      lv_obj_t *lbl_title = lv_label_create(tile);
      lv_label_set_text(lbl_title, title);
      lv_obj_set_style_text_color(lbl_title, kTextPrimary, LV_PART_MAIN);
      lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, LV_PART_MAIN);
      lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

      if (subtitle != nullptr) {
        lv_obj_t *lbl_sub = lv_label_create(tile);
        lv_label_set_text(lbl_sub, subtitle);
        lv_obj_set_style_text_color(lbl_sub, kTextSecondary, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align_to(lbl_sub, lbl_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
      }

      lv_obj_t *lbl_body = lv_label_create(tile);
      lv_label_set_text(lbl_body, body);
      lv_obj_set_style_text_color(lbl_body, kTextSecondary, LV_PART_MAIN);
      lv_obj_set_style_text_font(lbl_body, &lv_font_montserrat_12, LV_PART_MAIN);
      lv_obj_align(lbl_body, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
  }

  return true;
}

bool LiveDashboard::begin(fs::FS &fs, const char *config_path, uint16_t screen_width, uint16_t screen_height, char lvgl_drive_letter) {
  return g_impl.begin(*this, fs, config_path, screen_width, screen_height, lvgl_drive_letter);
}

void LiveDashboard::tick() { g_impl.tick(); }

bool LiveDashboard::publishGauge(const char *gauge_id, int32_t value, const char *text) { return g_impl.publishGauge(gauge_id, value, text); }

bool LiveDashboard::onAction(const char *action_id, ActionCallback cb, void *user) { return g_impl.onAction(action_id, cb, user); }

const char *LiveDashboard::robotName() const { return g_impl.robotName(); }

} // namespace live_dashboard
