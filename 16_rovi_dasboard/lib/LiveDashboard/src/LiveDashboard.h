#pragma once

#include <cstddef>
#include <cstdint>

#include <FS.h>

namespace live_dashboard {

#ifndef LIVE_DASHBOARD_MAX_TILES
#define LIVE_DASHBOARD_MAX_TILES 24
#endif

#ifndef LIVE_DASHBOARD_MAX_GAUGES
#define LIVE_DASHBOARD_MAX_GAUGES 24
#endif

#ifndef LIVE_DASHBOARD_MAX_BUTTONS
#define LIVE_DASHBOARD_MAX_BUTTONS 24
#endif

#ifndef LIVE_DASHBOARD_ID_MAX_LEN
#define LIVE_DASHBOARD_ID_MAX_LEN 32
#endif

class LiveDashboard {
public:
  using ActionCallback = void (*)(const char *action_id, void *user);

  LiveDashboard() = default;

  bool begin(fs::FS &fs, const char *config_path, uint16_t screen_width, uint16_t screen_height, char lvgl_drive_letter);
  void tick();

  bool publishGauge(const char *gauge_id, int32_t value, const char *text);
  bool onAction(const char *action_id, ActionCallback cb, void *user);

  const char *robotName() const;
};

} // namespace live_dashboard
