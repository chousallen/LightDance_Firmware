// sd_mount.h
#ifndef SD_MOUNT_H
#define SD_MOUNT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount SD card to /sd directory
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mount_sdcard(void);

/**
 * @brief Unmount SD card
 */
void unmount_sdcard(void);

/**
 * @brief Get player ID from SD card volume label
 * @return Player ID (1-31) if label is "LPSxx", 0 otherwise
 */
int get_sd_card_id(void);

#ifdef __cplusplus
}
#endif

#endif // SD_MOUNT_H