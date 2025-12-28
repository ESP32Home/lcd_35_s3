# 17_fs_sd_drive

Mount internal flash FATFS (`ffat` partition) and SD card (SD_MMC) and expose both as USB Mass Storage (two LUNs).

## What you get

- USB drive #1: internal flash FATFS (`ffat`) uploaded via `uploadfs`
- USB drive #2: SD card (if present)
- USB serial logs at 115200

## Build + flash

```bash
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35 -t upload
pio run -d 17_fs_sd_drive -e esp32-s3-touch-lcd-35 -t uploadfs
```

Then reboot and connect the board to your PC via the native USB port.

## Notes

- Always **eject/safely remove** the drives on your PC before resetting the ESP32-S3 to avoid filesystem corruption.
- The SD card uses the same pins/mode as sample `14_lvgl_image` (1-bit SDMMC).
