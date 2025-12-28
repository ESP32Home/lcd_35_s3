# 17_fs_sd_drive

Expose internal flash FATFS (`ffat` partition) as a USB Mass Storage drive.

## What you get

- USB drive: internal flash FATFS (`ffat`) uploaded via `uploadfs`
- USB serial logs at 115200

## Build + flash

```bash
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35 -t upload
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35 -t uploadfs
```

Then reboot and connect the board to your PC via the native USB port.

## Optional: enable SD card as 2nd drive

This sample can also expose the SD card as a second USB drive, but it is **disabled by default**.

- Enable by adding to `17_fs_sd_drive/platformio.ini`:
  - `build_flags = -DFS_SD_DRIVE_ENABLE_SD=1`

## Notes

- Always **eject/safely remove** the drives on your PC before resetting the ESP32-S3 to avoid filesystem corruption.
- The SD card uses the same pins/mode as sample `14_lvgl_image` (1-bit SDMMC).
