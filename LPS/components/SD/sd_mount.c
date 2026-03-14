#include "sd_mount.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "ff.h"

#include "control_reader.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t* g_sd_card = NULL;
static const char* TAG = "SD";

esp_err_t mount_sdcard(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.gpio_cd = GPIO_NUM_NC;
    slot_config.gpio_wp = GPIO_NUM_NC;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot_config, &mount_config, &g_sd_card);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed (%s)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SD card ID : %d", get_sd_card_id());
    return ESP_OK;
}

void unmount_sdcard(void)
{
    if (g_sd_card) {
        esp_vfs_fat_sdcard_unmount("/sd", g_sd_card);
        g_sd_card = NULL;
    }
}


/* ---- get sd card id ---- */
int get_sd_card_id(void) {
    if(g_sd_card == NULL) {
        return 0;
    }
    
    char volume_label[20];
    FRESULT res = f_getlabel("0:", volume_label, NULL);
    
    if(res != FR_OK || volume_label[0] == '\0') {
        return 0;
    }
    if(strncmp(volume_label, "LPS", 3) != 0) {
        return 0;
    }
    
    char* num_str = volume_label + 3;
    int id = atoi(num_str);
    
    if(id >= 1 && id <= 31) {
        return id;
    }
    
    return 0;
}

