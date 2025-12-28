#include <Arduino.h>

#include <FFat.h>

#include <USB.h>
#include <USBMSC.h>

extern "C" {
#include "esp_partition.h"
#include "wear_levelling.h"
}

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static constexpr int kSdClk = 11;
static constexpr int kSdCmd = 10;
static constexpr int kSdD0 = 9;

static USBMSC g_msc_flash;
static USBMSC g_msc_sd;

static wl_handle_t g_flash_wl = WL_INVALID_HANDLE;
static uint32_t g_flash_block_count = 0;
static uint16_t g_flash_block_size = 0;

static bool g_flash_cache_valid = false;
static bool g_flash_cache_dirty = false;
static uint32_t g_flash_cache_lba = 0;
static uint8_t g_flash_cache[4096];

static bool g_sd_ready = false;
static sdmmc_card_t g_sd_card{};
static uint32_t g_sd_block_count = 0;
static uint16_t g_sd_block_size = 512;

static bool g_sd_cache_valid = false;
static bool g_sd_cache_dirty = false;
static uint32_t g_sd_cache_lba = 0;
static uint8_t g_sd_cache[512];

static bool flash_commit_cache_() {
  if (!g_flash_cache_valid || !g_flash_cache_dirty || g_flash_wl == WL_INVALID_HANDLE) {
    return true;
  }

  const size_t addr = static_cast<size_t>(g_flash_cache_lba) * g_flash_block_size;
  esp_err_t err = wl_erase_range(g_flash_wl, addr, g_flash_block_size);
  if (err != ESP_OK) {
    Serial.printf("FLASH: wl_erase_range failed: %d\n", static_cast<int>(err));
    return false;
  }

  err = wl_write(g_flash_wl, addr, g_flash_cache, g_flash_block_size);
  if (err != ESP_OK) {
    Serial.printf("FLASH: wl_write failed: %d\n", static_cast<int>(err));
    return false;
  }

  g_flash_cache_dirty = false;
  return true;
}

static int32_t flash_read_cb_(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (g_flash_wl == WL_INVALID_HANDLE) {
    return 0;
  }
  if (buffer == nullptr || bufsize == 0) {
    return 0;
  }
  if (g_flash_block_size == 0 || offset + bufsize > g_flash_block_size) {
    Serial.printf("FLASH: read OOB lba=%u off=%u size=%u (blk=%u)\n",
                  static_cast<unsigned>(lba),
                  static_cast<unsigned>(offset),
                  static_cast<unsigned>(bufsize),
                  static_cast<unsigned>(g_flash_block_size));
    return 0;
  }

  if (g_flash_cache_valid && g_flash_cache_lba == lba) {
    memcpy(buffer, g_flash_cache + offset, bufsize);
    return static_cast<int32_t>(bufsize);
  }

  esp_err_t err = wl_read(g_flash_wl, static_cast<size_t>(lba) * g_flash_block_size + offset, buffer, bufsize);
  if (err != ESP_OK) {
    Serial.printf("FLASH: wl_read failed: %d\n", static_cast<int>(err));
    return 0;
  }
  return static_cast<int32_t>(bufsize);
}

static int32_t flash_write_cb_(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (g_flash_wl == WL_INVALID_HANDLE) {
    return 0;
  }
  if (buffer == nullptr || bufsize == 0) {
    return 0;
  }
  if (g_flash_block_size == 0 || offset + bufsize > g_flash_block_size) {
    Serial.printf("FLASH: write OOB lba=%u off=%u size=%u (blk=%u)\n",
                  static_cast<unsigned>(lba),
                  static_cast<unsigned>(offset),
                  static_cast<unsigned>(bufsize),
                  static_cast<unsigned>(g_flash_block_size));
    return 0;
  }

  if (!g_flash_cache_valid || g_flash_cache_lba != lba) {
    if (!flash_commit_cache_()) {
      return 0;
    }

    esp_err_t err = wl_read(g_flash_wl, static_cast<size_t>(lba) * g_flash_block_size, g_flash_cache, g_flash_block_size);
    if (err != ESP_OK) {
      Serial.printf("FLASH: wl_read(block) failed: %d\n", static_cast<int>(err));
      return 0;
    }

    g_flash_cache_valid = true;
    g_flash_cache_dirty = false;
    g_flash_cache_lba = lba;
  }

  memcpy(g_flash_cache + offset, buffer, bufsize);
  g_flash_cache_dirty = true;

  if (offset + bufsize >= g_flash_block_size) {
    if (!flash_commit_cache_()) {
      return 0;
    }
  }

  return static_cast<int32_t>(bufsize);
}

static bool flash_start_stop_cb_(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  Serial.printf("FLASH MSC: start=%u eject=%u\n", static_cast<unsigned>(start), static_cast<unsigned>(load_eject));

  if (!start && load_eject) {
    (void)flash_commit_cache_();
  }
  return true;
}

static bool sd_commit_cache_() {
  if (!g_sd_cache_valid || !g_sd_cache_dirty || !g_sd_ready) {
    return true;
  }

  esp_err_t err = sdmmc_write_sectors(&g_sd_card, g_sd_cache, g_sd_cache_lba, 1);
  if (err != ESP_OK) {
    Serial.printf("SD: sdmmc_write_sectors failed (lba=%u): %d\n",
                  static_cast<unsigned>(g_sd_cache_lba),
                  static_cast<int>(err));
    return false;
  }
  g_sd_cache_dirty = false;
  return true;
}

static bool sd_load_cache_(uint32_t lba) {
  if (!g_sd_ready) {
    return false;
  }

  esp_err_t err = sdmmc_read_sectors(&g_sd_card, g_sd_cache, lba, 1);
  if (err != ESP_OK) {
    Serial.printf("SD: sdmmc_read_sectors failed (lba=%u): %d\n", static_cast<unsigned>(lba), static_cast<int>(err));
    return false;
  }
  g_sd_cache_valid = true;
  g_sd_cache_dirty = false;
  g_sd_cache_lba = lba;
  return true;
}

static int32_t sd_read_cb_(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (!g_sd_ready) {
    return 0;
  }
  if (buffer == nullptr || bufsize == 0) {
    return 0;
  }
  if (offset + bufsize > g_sd_block_size) {
    Serial.printf("SD: read OOB lba=%u off=%u size=%u (blk=%u)\n",
                  static_cast<unsigned>(lba),
                  static_cast<unsigned>(offset),
                  static_cast<unsigned>(bufsize),
                  static_cast<unsigned>(g_sd_block_size));
    return 0;
  }

  if (!g_sd_cache_valid || g_sd_cache_lba != lba) {
    if (!sd_commit_cache_()) {
      return 0;
    }
    if (!sd_load_cache_(lba)) {
      return 0;
    }
  }

  memcpy(buffer, g_sd_cache + offset, bufsize);
  return static_cast<int32_t>(bufsize);
}

static int32_t sd_write_cb_(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (!g_sd_ready) {
    return 0;
  }
  if (buffer == nullptr || bufsize == 0) {
    return 0;
  }
  if (offset + bufsize > g_sd_block_size) {
    Serial.printf("SD: write OOB lba=%u off=%u size=%u (blk=%u)\n",
                  static_cast<unsigned>(lba),
                  static_cast<unsigned>(offset),
                  static_cast<unsigned>(bufsize),
                  static_cast<unsigned>(g_sd_block_size));
    return 0;
  }

  if (!g_sd_cache_valid || g_sd_cache_lba != lba) {
    if (!sd_commit_cache_()) {
      return 0;
    }
    if (!sd_load_cache_(lba)) {
      memset(g_sd_cache, 0, sizeof(g_sd_cache));
      g_sd_cache_valid = true;
      g_sd_cache_dirty = false;
      g_sd_cache_lba = lba;
    }
  }

  memcpy(g_sd_cache + offset, buffer, bufsize);
  g_sd_cache_dirty = true;

  if (offset + bufsize >= g_sd_block_size) {
    if (!sd_commit_cache_()) {
      return 0;
    }
  }

  return static_cast<int32_t>(bufsize);
}

static bool sd_start_stop_cb_(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  Serial.printf("SD MSC: start=%u eject=%u\n", static_cast<unsigned>(start), static_cast<unsigned>(load_eject));

  if (!start && load_eject) {
    (void)sd_commit_cache_();
  }
  return true;
}

static bool mount_flash_for_msc_() {
  const esp_partition_t *part =
    esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, FFAT_PARTITION_LABEL);
  if (part == nullptr) {
    Serial.println("FLASH: ffat partition not found");
    return false;
  }

  wl_handle_t handle = WL_INVALID_HANDLE;
  esp_err_t err = wl_mount(part, &handle);
  if (err != ESP_OK) {
    Serial.printf("FLASH: wl_mount failed: %d\n", static_cast<int>(err));
    return false;
  }

  const size_t sector = wl_sector_size(handle);
  const size_t total = wl_size(handle);
  if (sector == 0 || total < sector || sector > sizeof(g_flash_cache) || sector > 0xFFFFu) {
    Serial.printf("FLASH: unsupported sector size: %u\n", static_cast<unsigned>(sector));
    wl_unmount(handle);
    return false;
  }

  g_flash_wl = handle;
  g_flash_block_size = static_cast<uint16_t>(sector);
  g_flash_block_count = static_cast<uint32_t>(total / sector);
  Serial.printf("FLASH: wl ok sector=%u blocks=%u total=%u\n",
                static_cast<unsigned>(g_flash_block_size),
                static_cast<unsigned>(g_flash_block_count),
                static_cast<unsigned>(total));
  return true;
}

static bool init_sd_1bit_() {
  const int clk_gpio = digitalPinToGPIONumber(kSdClk);
  const int cmd_gpio = digitalPinToGPIONumber(kSdCmd);
  const int d0_gpio = digitalPinToGPIONumber(kSdD0);

  if (clk_gpio < 0 || cmd_gpio < 0 || d0_gpio < 0) {
    Serial.printf("SD: invalid pins clk=%d cmd=%d d0=%d\n", clk_gpio, cmd_gpio, d0_gpio);
    return false;
  }

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.slot = SDMMC_HOST_SLOT_1;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  slot_config.clk = static_cast<gpio_num_t>(clk_gpio);
  slot_config.cmd = static_cast<gpio_num_t>(cmd_gpio);
  slot_config.d0 = static_cast<gpio_num_t>(d0_gpio);
  slot_config.d1 = GPIO_NUM_NC;
  slot_config.d2 = GPIO_NUM_NC;
  slot_config.d3 = GPIO_NUM_NC;

  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK) {
    Serial.printf("SD: sdmmc_host_init failed: %d\n", static_cast<int>(err));
    return false;
  }

  err = sdmmc_host_init_slot(host.slot, &slot_config);
  if (err != ESP_OK) {
    Serial.printf("SD: sdmmc_host_init_slot failed: %d\n", static_cast<int>(err));
    sdmmc_host_deinit();
    return false;
  }

  err = sdmmc_card_init(&host, &g_sd_card);
  if (err != ESP_OK) {
    Serial.printf("SD: sdmmc_card_init failed: %d\n", static_cast<int>(err));
    sdmmc_host_deinit();
    return false;
  }

  if (g_sd_card.csd.sector_size == 0 || g_sd_card.csd.capacity == 0) {
    Serial.println("SD: invalid card CSD");
    sdmmc_host_deinit();
    return false;
  }

  if (g_sd_card.csd.sector_size != 512) {
    Serial.printf("SD: unsupported sector size: %u (expected 512)\n", static_cast<unsigned>(g_sd_card.csd.sector_size));
    sdmmc_host_deinit();
    return false;
  }

  g_sd_block_size = static_cast<uint16_t>(g_sd_card.csd.sector_size);
  g_sd_block_count = static_cast<uint32_t>(g_sd_card.csd.capacity);
  g_sd_ready = true;

  const uint64_t bytes = static_cast<uint64_t>(g_sd_block_count) * static_cast<uint64_t>(g_sd_block_size);
  Serial.printf("SD: ready, size=%llu MB, sectors=%u, sector=%u\n",
                bytes / (1024ULL * 1024ULL),
                static_cast<unsigned>(g_sd_block_count),
                static_cast<unsigned>(g_sd_block_size));
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("17_fs_sd_drive: internal ffat + SD_MMC as USB MSC (2 drives)");

  // Init SD card (same wiring as sample 14_lvgl_image, 1-bit SDMMC).
  if (!init_sd_1bit_()) {
    Serial.println("SD: init failed (no card?)");
  }

  // Optional: mount FFat briefly to prove the FS is OK (then unmount, since USB MSC will own it).
  if (FFat.begin(false)) {
    Serial.printf("FFat: mounted at /ffat, used=%u / total=%u\n",
                  static_cast<unsigned>(FFat.usedBytes()),
                  static_cast<unsigned>(FFat.totalBytes()));
    FFat.end();
  } else {
    Serial.println("FFat: mount failed (did you run uploadfs?)");
  }

  const bool flash_ok = mount_flash_for_msc_();

  g_msc_flash.vendorID("ESP32");
  g_msc_flash.productID("FFAT");
  g_msc_flash.productRevision("1.0");
  g_msc_flash.onRead(flash_read_cb_);
  g_msc_flash.onWrite(flash_write_cb_);
  g_msc_flash.onStartStop(flash_start_stop_cb_);

  if (flash_ok && g_msc_flash.begin(g_flash_block_count, g_flash_block_size)) {
    g_msc_flash.mediaPresent(true);
    Serial.println("MSC: flash LUN enabled");
  } else {
    g_msc_flash.mediaPresent(false);
    Serial.println("MSC: flash LUN disabled");
  }

  g_msc_sd.vendorID("ESP32");
  g_msc_sd.productID("SDMMC");
  g_msc_sd.productRevision("1.0");
  g_msc_sd.onRead(sd_read_cb_);
  g_msc_sd.onWrite(sd_write_cb_);
  g_msc_sd.onStartStop(sd_start_stop_cb_);

  if (g_sd_ready && g_msc_sd.begin(g_sd_block_count, g_sd_block_size)) {
    g_msc_sd.mediaPresent(true);
    Serial.println("MSC: SD LUN enabled");
  } else {
    g_msc_sd.mediaPresent(false);
    Serial.println("MSC: SD LUN disabled");
  }

  const bool usb_ok = USB.begin();
  Serial.printf("USB.begin(): %s\n", usb_ok ? "ok" : "failed");
  Serial.println("Ready: connect USB to PC to mount the drives.");
}

void loop() {
  delay(100);
}
