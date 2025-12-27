#pragma once

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

namespace live_dashboard {

struct Stage {
  int32_t threshold;
  lv_color_t color;
};

lv_color_t PickStageColor(int32_t value, const Stage *stages, size_t stage_count, lv_color_t fallback);

bool ShowSplashFromLvglPath(const char *lvgl_path,
                            lv_coord_t screen_width,
                            lv_coord_t screen_height,
                            uint32_t duration_ms,
                            lv_color_t background_color = lv_color_hex(0x0B1220));

struct ArcGaugeConfig {
  const char *title = "";
  int32_t min_value = 0;
  int32_t max_value = 100;
  int32_t initial_value = 0;
  const char *initial_text = "";
  const char *min_label = nullptr;
  const char *max_label = nullptr;
  lv_color_t accent_color = lv_palette_main(LV_PALETTE_BLUE);
  const Stage *stages = nullptr;
  size_t stage_count = 0;
  lv_color_t stages_fallback_color = lv_palette_main(LV_PALETTE_GREEN);
};

class ArcGaugeTile {
public:
  ArcGaugeTile() = default;

  lv_obj_t *create(lv_obj_t *parent, const ArcGaugeConfig &config);

  void setStaleTimeout(uint32_t timeout_ms);
  void setStaleText(const char *text);

  void publish(int32_t value, const char *value_text, uint32_t now_ms = lv_tick_get());
  void tick(uint32_t now_ms = lv_tick_get());

  bool isStale() const;

  lv_obj_t *tile() const;
  lv_obj_t *arc() const;
  lv_obj_t *valueLabel() const;

private:
  void applyFresh_(int32_t value, const char *value_text);
  void applyStale_();
  lv_color_t indicatorColorForValue_(int32_t value) const;

  lv_obj_t *tile_ = nullptr;
  lv_obj_t *arc_ = nullptr;
  lv_obj_t *value_label_ = nullptr;

  int32_t min_value_ = 0;
  int32_t max_value_ = 100;
  int32_t value_ = 0;
  int32_t last_drawn_value_ = 0;
  bool has_value_ = false;
  bool is_stale_ = true;
  uint32_t last_update_ms_ = 0;
  uint32_t stale_timeout_ms_ = 0;
  const char *stale_text_ = "--";

  lv_color_t accent_color_ = lv_palette_main(LV_PALETTE_BLUE);
  const Stage *stages_ = nullptr;
  size_t stage_count_ = 0;
  lv_color_t stages_fallback_color_ = lv_palette_main(LV_PALETTE_GREEN);
};

struct ButtonConfig {
  const char *tile_title = "";
  const char *button_label = "";
  lv_color_t button_color = lv_palette_main(LV_PALETTE_BLUE);
  lv_coord_t button_height = 95;
};

struct InfoTileConfig {
  const char *title = "";
  const char *subtitle = "";
  const char *body = "";
};

struct RosTileConfig {
  const char *title = "";
  const char *status = "";
  lv_color_t status_color = lv_palette_main(LV_PALETTE_RED);
  const char *body = "";
};

struct LiveDashboardConfig {
  lv_coord_t screen_width = 0;
  lv_coord_t screen_height = 0;
  uint32_t stale_timeout_ms = 5000;
  lv_color_t background_color = lv_color_hex(0x0B1220);
  bool dark_theme = true;

  ArcGaugeConfig voltage_gauge{};
  ArcGaugeConfig cpu_gauge{};
  ButtonConfig shutdown_button{};
  ButtonConfig restart_button{};
  InfoTileConfig info_tile{};
  RosTileConfig ros_tile{};
};

class LiveDashboard {
public:
  LiveDashboard() = default;

  void create(const LiveDashboardConfig &config);
  void tick(uint32_t now_ms = lv_tick_get());

  void publishVoltage(int32_t value, const char *text, uint32_t now_ms = lv_tick_get());
  void publishCpu(int32_t value, const char *text, uint32_t now_ms = lv_tick_get());

  ArcGaugeTile &voltageGauge();
  ArcGaugeTile &cpuGauge();

  lv_obj_t *shutdownButton() const;
  lv_obj_t *restartButton() const;

  lv_obj_t *rosStatusLabel() const;

private:
  static lv_obj_t *createTile_(lv_obj_t *parent);

  lv_obj_t *grid_ = nullptr;
  lv_obj_t *shutdown_button_ = nullptr;
  lv_obj_t *restart_button_ = nullptr;
  lv_obj_t *ros_status_label_ = nullptr;

  ArcGaugeTile voltage_gauge_{};
  ArcGaugeTile cpu_gauge_{};

  lv_coord_t col_dsc_[3]{0, 0, LV_GRID_TEMPLATE_LAST};
  lv_coord_t row_dsc_[4]{0, 0, 0, LV_GRID_TEMPLATE_LAST};
};

} // namespace live_dashboard
