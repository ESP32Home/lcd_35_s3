# 16_rovi_dasboard

Simple LVGL dashboard demo for a robot (ROVI), targeting the 3.5" 320×480 ESP32‑S3 Touch LCD.

## What you get

- 2×3 fixed tile layout (matches the 320×480 display)
  - Voltage arc gauge (multi‑stage coloring)
  - CPU arc gauge
  - Shutdown / Restart buttons (demo messagebox)
  - 2 info tiles (static text placeholders)
- Fail‑safe display: if a gauge value is older than `stale_timeout_ms` it shows `--`
- Optional SD splash screen (PNG) before showing the dashboard

## Local library

This sample refactors the UI into a local PlatformIO library:

- `lib/LiveDashboard/` (rovi‑neutral)
  - `live_dashboard::ArcGaugeTile` (arc gauge + stale handling + optional stage colors)
  - `live_dashboard::LiveDashboard` (builds the 2×3 dashboard layout)
  - `live_dashboard::ShowSplashFromLvglPath(...)` (loads and shows an image via LVGL FS)

The rovi‑specific parts stay in `src/main.cpp`:

- Battery voltage stage thresholds + colors (`kBatteryVoltageStages`)
- Splash image path (`kSplashLvglPath`) and duration
- Demo timer that publishes dummy Voltage/CPU values

## Splash screen (SD card)

1. Copy `data/rovi.png` to the SD card root as `rovi.png`.
2. Insert SD card and boot.
3. The code tries to load `S:rovi.png` for ~3 seconds; if SD isn’t mounted or the file can’t be decoded it prints a message and continues.

Nothing needs to be “flashed” for the splash image when loading from SD: it’s read at runtime from the card.

## Internal config (FFat)

- `data/config.json` is built into the internal flash FATFS partition (`ffat` in `partitions/partitions_16MB_3MBapp_9_9MB_fatfs.csv`).
- `/config.json` is required: if it’s missing or invalid the firmware prints a fatal message and shows an empty “CONFIG ERROR” screen.

Upload the filesystem image:

- `pio run -e esp32-s3-touch-lcd-35 -t uploadfs`

## Updating values

In `src/main.cpp` the demo uses:

- `g_dashboard.publishVoltage(voltage_x10, "12.1V")`
- `g_dashboard.publishCpu(cpu_percent, "37%")`
- `g_dashboard.tick()` (called from `loop()` to enforce stale/`--` behavior)

To change multi‑stage thresholds/colors, edit `kBatteryVoltageStages` in `src/main.cpp`.

## Build (PlatformIO)

From the project folder:

- `pio run -e esp32-s3-touch-lcd-35`
- `pio run -e esp32-s3-touch-lcd-35 -t upload`
