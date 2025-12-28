# 16_rovi_dasboard

Simple LVGL dashboard demo for a robot (ROVI), targeting the 3.5" 320×480 ESP32‑S3 Touch LCD.

## What you get

- Grid layout (configured in `data/config.json`, default is 2×3 for 320×480)
  - Voltage arc gauge (multi‑stage coloring)
  - CPU arc gauge
  - Shutdown / Restart buttons (serial callbacks)
  - 2 text tiles (static placeholders)
- Fail‑safe display: if a gauge value is older than `ui.stale_timeout_ms` it shows `--`
- Splash screen (PNG) loaded from internal flash FS (FFat) before showing the dashboard

## Local library

This sample uses two local PlatformIO libraries:

- `lib/WsLcd35S3Hal/` (board/HAL)
  - Brings up Arduino_GFX + touch + LVGL display/input
  - Mounts internal FFat and registers it as LVGL drive `F:`
- `lib/LiveDashboard/` (UI)
  - Loads `/config.json` from internal FFat
  - Builds the tile grid + widgets from config
  - Shows splash from internal FFat (e.g. `F:/rovi.png`)

`src/main.cpp` stays minimal:

- Registers callbacks for `shutdown` / `restart`
- Publishes dummy Voltage/CPU values on a timer (to show colors + stale state)

## Internal config (FFat)

- `data/config.json` and `data/rovi.png` are built into the internal flash FATFS partition (`ffat` in `partitions/partitions_16MB_3MBapp_9_9MB_fatfs.csv`).
- `/config.json` is required: if it’s missing or invalid the firmware prints a fatal message and shows a “CONFIG ERROR” screen.

Upload the filesystem image:

- `pio run -e esp32-s3-touch-lcd-35 -t uploadfs`

## Updating values

In `src/main.cpp` the demo uses:

- `g_dashboard.publishGauge("voltage", voltage_x10, "12.1V")`
- `g_dashboard.publishGauge("cpu", cpu_percent, "37%")`
- `g_dashboard.tick()` (called from `loop()` to enforce stale/`--` behavior)

## Demo input (JSONL + Serial)

The sample includes a simple “event replay” demo:

- `data/test.jsonl` is uploaded to internal FFat and replayed once per second (loops at EOF).
- You can also paste the same JSON lines into the serial monitor; the line is applied when a full newline is received.
- Disable it by setting `ROVI_ENABLE_DEMO` to `0` in `src/main.cpp`.

Line format:

- Single update: `{"id":"voltage","value":121,"text":"12.1V"}`
- Multiple updates in one line: `[{"id":"voltage","value":121,"text":"12.1V"},{"id":"cpu","value":37,"text":"37%"}]`

## Build (PlatformIO)

From the project folder:

- `pio run -e esp32-s3-touch-lcd-35`
- `pio run -e esp32-s3-touch-lcd-35 -t upload`
