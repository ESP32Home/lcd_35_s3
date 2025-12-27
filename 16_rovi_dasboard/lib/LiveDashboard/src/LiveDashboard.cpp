#include "LiveDashboard.h"

#include <Arduino.h>

namespace live_dashboard {
namespace {

static const lv_color_t kTileBg = lv_color_hex(0x111827);
static const lv_color_t kTileBorder = lv_color_hex(0x0B1220);
static const lv_color_t kTextPrimary = lv_color_hex(0xE2E8F0);
static const lv_color_t kTextSecondary = lv_color_hex(0x94A3B8);
static const lv_color_t kArcBg = lv_color_hex(0x334155);
static const lv_color_t kStaleArc = lv_color_hex(0x475569);

lv_obj_t *create_tile(lv_obj_t *parent) {
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

} // namespace

lv_color_t PickStageColor(int32_t value, const Stage *stages, size_t stage_count, lv_color_t fallback) {
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

bool ShowSplashFromLvglPath(const char *lvgl_path,
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
  if (zoom > 256U) {
    zoom = 256U;
  }
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

lv_obj_t *ArcGaugeTile::create(lv_obj_t *parent, const ArcGaugeConfig &config) {
  if (parent == nullptr) {
    return nullptr;
  }

  tile_ = create_tile(parent);

  min_value_ = config.min_value;
  max_value_ = config.max_value;
  accent_color_ = config.accent_color;
  stages_ = config.stages;
  stage_count_ = config.stage_count;
  stages_fallback_color_ = config.stages_fallback_color;

  lv_obj_t *title_label = lv_label_create(tile_);
  lv_label_set_text(title_label, config.title != nullptr ? config.title : "");
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
  lv_label_set_text(value_label_, config.initial_text != nullptr ? config.initial_text : "");
  lv_obj_set_width(value_label_, LV_PCT(100));
  lv_obj_set_style_text_align(value_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(value_label_, LV_ALIGN_CENTER, 0, 14);

  if (config.min_label != nullptr && config.max_label != nullptr) {
    lv_obj_t *min_value_label = lv_label_create(tile_);
    lv_label_set_text(min_value_label, config.min_label);
    lv_obj_set_style_text_color(min_value_label, kTextSecondary, LV_PART_MAIN);
    lv_obj_set_style_text_font(min_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(min_value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *max_value_label = lv_label_create(tile_);
    lv_label_set_text(max_value_label, config.max_label);
    lv_obj_set_style_text_color(max_value_label, kTextSecondary, LV_PART_MAIN);
    lv_obj_set_style_text_font(max_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(max_value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  }

  publish(config.initial_value, config.initial_text, lv_tick_get());

  return tile_;
}

void ArcGaugeTile::setStaleTimeout(uint32_t timeout_ms) { stale_timeout_ms_ = timeout_ms; }

void ArcGaugeTile::setStaleText(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    stale_text_ = "--";
    return;
  }
  stale_text_ = text;
}

void ArcGaugeTile::publish(int32_t value, const char *value_text, uint32_t now_ms) {
  value_ = value;
  last_update_ms_ = now_ms;
  has_value_ = true;

  if (is_stale_ || last_drawn_value_ != value_) {
    is_stale_ = false;
    last_drawn_value_ = value_;
    applyFresh_(value_, value_text);
  } else if (!is_stale_) {
    applyFresh_(value_, value_text);
  }
}

void ArcGaugeTile::tick(uint32_t now_ms) {
  if (arc_ == nullptr || value_label_ == nullptr) {
    return;
  }

  bool stale = !has_value_ || (stale_timeout_ms_ > 0 && (now_ms - last_update_ms_ > stale_timeout_ms_));
  if (stale) {
    if (!is_stale_) {
      is_stale_ = true;
      applyStale_();
    }
  }
}

bool ArcGaugeTile::isStale() const { return is_stale_; }

lv_obj_t *ArcGaugeTile::tile() const { return tile_; }
lv_obj_t *ArcGaugeTile::arc() const { return arc_; }
lv_obj_t *ArcGaugeTile::valueLabel() const { return value_label_; }

lv_color_t ArcGaugeTile::indicatorColorForValue_(int32_t value) const {
  if (stages_ != nullptr && stage_count_ > 0) {
    return PickStageColor(value, stages_, stage_count_, stages_fallback_color_);
  }
  return accent_color_;
}

void ArcGaugeTile::applyFresh_(int32_t value, const char *value_text) {
  if (arc_ == nullptr || value_label_ == nullptr) {
    return;
  }

  if (value < min_value_) {
    value = min_value_;
  } else if (value > max_value_) {
    value = max_value_;
  }

  lv_arc_set_value(arc_, value);
  lv_obj_set_style_arc_color(arc_, indicatorColorForValue_(value), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(value_label_, kTextPrimary, LV_PART_MAIN);
  lv_label_set_text(value_label_, value_text != nullptr ? value_text : "");
}

void ArcGaugeTile::applyStale_() {
  if (arc_ == nullptr || value_label_ == nullptr) {
    return;
  }

  lv_arc_set_value(arc_, min_value_);
  lv_obj_set_style_arc_color(arc_, kStaleArc, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(value_label_, kTextSecondary, LV_PART_MAIN);
  lv_label_set_text(value_label_, stale_text_);
}

void LiveDashboard::create(const LiveDashboardConfig &config) {
  lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(),
                                           lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_RED),
                                           config.dark_theme,
                                           LV_FONT_DEFAULT);
  lv_disp_set_theme(lv_disp_get_default(), theme);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, config.background_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  col_dsc_[0] = static_cast<lv_coord_t>(config.screen_width / 2);
  col_dsc_[1] = static_cast<lv_coord_t>(config.screen_width / 2);
  col_dsc_[2] = LV_GRID_TEMPLATE_LAST;

  row_dsc_[0] = static_cast<lv_coord_t>(config.screen_height / 3);
  row_dsc_[1] = static_cast<lv_coord_t>(config.screen_height / 3);
  row_dsc_[2] = static_cast<lv_coord_t>(config.screen_height / 3);
  row_dsc_[3] = LV_GRID_TEMPLATE_LAST;

  grid_ = lv_obj_create(scr);
  lv_obj_set_size(grid_, config.screen_width, config.screen_height);
  lv_obj_set_style_bg_opa(grid_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(grid_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(grid_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(grid_, 0, LV_PART_MAIN);
  lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(grid_, col_dsc_, row_dsc_);

  lv_obj_t *tile_voltage = voltage_gauge_.create(grid_, config.voltage_gauge);
  lv_obj_set_grid_cell(tile_voltage, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_t *tile_cpu = cpu_gauge_.create(grid_, config.cpu_gauge);
  lv_obj_set_grid_cell(tile_cpu, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_t *tile_shutdown = createTile_(grid_);
  lv_obj_set_grid_cell(tile_shutdown, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t *shutdown_title = lv_label_create(tile_shutdown);
  lv_label_set_text(shutdown_title, config.shutdown_button.tile_title);
  lv_obj_set_style_text_color(shutdown_title, kTextPrimary, LV_PART_MAIN);
  lv_obj_set_style_text_font(shutdown_title, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(shutdown_title, LV_ALIGN_TOP_MID, 0, 0);

  shutdown_button_ = lv_btn_create(tile_shutdown);
  lv_obj_set_size(shutdown_button_, LV_PCT(100), config.shutdown_button.button_height);
  lv_obj_align(shutdown_button_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(shutdown_button_, config.shutdown_button.button_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(shutdown_button_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(shutdown_button_, 12, LV_PART_MAIN);
  lv_obj_t *lbl_shutdown = lv_label_create(shutdown_button_);
  lv_label_set_text(lbl_shutdown, config.shutdown_button.button_label);
  lv_obj_center(lbl_shutdown);

  lv_obj_t *tile_restart = createTile_(grid_);
  lv_obj_set_grid_cell(tile_restart, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_t *restart_title = lv_label_create(tile_restart);
  lv_label_set_text(restart_title, config.restart_button.tile_title);
  lv_obj_set_style_text_color(restart_title, kTextPrimary, LV_PART_MAIN);
  lv_obj_set_style_text_font(restart_title, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(restart_title, LV_ALIGN_TOP_MID, 0, 0);

  restart_button_ = lv_btn_create(tile_restart);
  lv_obj_set_size(restart_button_, LV_PCT(100), config.restart_button.button_height);
  lv_obj_align(restart_button_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(restart_button_, config.restart_button.button_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(restart_button_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(restart_button_, 12, LV_PART_MAIN);
  lv_obj_t *lbl_restart = lv_label_create(restart_button_);
  lv_label_set_text(lbl_restart, config.restart_button.button_label);
  lv_obj_center(lbl_restart);

  lv_obj_t *tile_info = createTile_(grid_);
  lv_obj_set_grid_cell(tile_info, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_t *lbl_info_title = lv_label_create(tile_info);
  lv_label_set_text(lbl_info_title, config.info_tile.title);
  lv_obj_set_style_text_color(lbl_info_title, kTextPrimary, LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_info_title, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(lbl_info_title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lbl_info_sub = lv_label_create(tile_info);
  lv_label_set_text(lbl_info_sub, config.info_tile.subtitle);
  lv_obj_set_style_text_color(lbl_info_sub, kTextSecondary, LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_info_sub, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align_to(lbl_info_sub, lbl_info_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lv_obj_t *lbl_info_body = lv_label_create(tile_info);
  lv_label_set_text(lbl_info_body, config.info_tile.body);
  lv_obj_set_style_text_color(lbl_info_body, kTextSecondary, LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_info_body, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_info_body, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *tile_ros = createTile_(grid_);
  lv_obj_set_grid_cell(tile_ros, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_t *lbl_ros = lv_label_create(tile_ros);
  lv_label_set_text(lbl_ros, config.ros_tile.title);
  lv_obj_set_style_text_color(lbl_ros, kTextPrimary, LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_ros, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(lbl_ros, LV_ALIGN_TOP_LEFT, 0, 0);

  ros_status_label_ = lv_label_create(tile_ros);
  lv_label_set_text(ros_status_label_, config.ros_tile.status);
  lv_obj_set_style_text_color(ros_status_label_, config.ros_tile.status_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(ros_status_label_, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align_to(ros_status_label_, lbl_ros, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

  lv_obj_t *lbl_ros_body = lv_label_create(tile_ros);
  lv_label_set_text(lbl_ros_body, config.ros_tile.body);
  lv_obj_set_style_text_color(lbl_ros_body, kTextSecondary, LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl_ros_body, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_obj_align(lbl_ros_body, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  voltage_gauge_.setStaleTimeout(config.stale_timeout_ms);
  cpu_gauge_.setStaleTimeout(config.stale_timeout_ms);
}

void LiveDashboard::tick(uint32_t now_ms) {
  voltage_gauge_.tick(now_ms);
  cpu_gauge_.tick(now_ms);
}

void LiveDashboard::publishVoltage(int32_t value, const char *text, uint32_t now_ms) { voltage_gauge_.publish(value, text, now_ms); }
void LiveDashboard::publishCpu(int32_t value, const char *text, uint32_t now_ms) { cpu_gauge_.publish(value, text, now_ms); }

ArcGaugeTile &LiveDashboard::voltageGauge() { return voltage_gauge_; }
ArcGaugeTile &LiveDashboard::cpuGauge() { return cpu_gauge_; }

lv_obj_t *LiveDashboard::shutdownButton() const { return shutdown_button_; }
lv_obj_t *LiveDashboard::restartButton() const { return restart_button_; }

lv_obj_t *LiveDashboard::rosStatusLabel() const { return ros_status_label_; }

lv_obj_t *LiveDashboard::createTile_(lv_obj_t *parent) {
  if (parent == nullptr) {
    return nullptr;
  }
  return create_tile(parent);
}

} // namespace live_dashboard
