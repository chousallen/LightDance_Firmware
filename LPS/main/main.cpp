#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bt_receiver.h"
#include "esp_err.h"
#include "ld_board.h"
#include "ld_config.h"
#include "ld_gamma_lut.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "player.hpp"
#include "readframe.h"
#include "sd_logger.h"
#include "sd_utils.h"
#include "tcp_client.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "APP";

// System state flags and global queue
static bool frame_sys_ready = false;
QueueHandle_t sys_cmd_queue = NULL;

static bool sd_mounted = false;
static bool logger_inited = false;
static bool frame_inited = false;

static void print_restart_reason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reason_name = "UNKNOWN";
    const char* reason_desc = "Reset reason is not recognized.";
    bool is_error = false;
    bool is_warning = false;

    switch(reason) {
        case ESP_RST_UNKNOWN:
            reason_name = "UNKNOWN";
            reason_desc = "Reset reason could not be determined.";
            is_warning = true;
            break;
        case ESP_RST_POWERON:
            reason_name = "POWERON";
            reason_desc = "Normal power-on reset.";
            break;
        case ESP_RST_EXT:
            reason_name = "EXT";
            reason_desc = "External reset signal triggered reboot.";
            break;
        case ESP_RST_SW:
            reason_name = "SW";
            reason_desc = "Software requested a restart.";
            is_warning = true;
            break;
        case ESP_RST_PANIC:
            reason_name = "PANIC";
            reason_desc = "System rebooted after a fatal exception.";
            is_error = true;
            break;
        case ESP_RST_INT_WDT:
            reason_name = "INT_WDT";
            reason_desc = "Interrupt watchdog timeout.";
            is_error = true;
            break;
        case ESP_RST_TASK_WDT:
            reason_name = "TASK_WDT";
            reason_desc = "Task watchdog timeout.";
            is_error = true;
            break;
        case ESP_RST_WDT:
            reason_name = "WDT";
            reason_desc = "Other watchdog triggered a reset.";
            is_error = true;
            break;
        case ESP_RST_DEEPSLEEP:
            reason_name = "DEEPSLEEP";
            reason_desc = "Wake-up from deep sleep.";
            break;
        case ESP_RST_BROWNOUT:
            reason_name = "BROWNOUT";
            reason_desc = "Power supply voltage dropped too low.";
            is_error = true;
            break;
        case ESP_RST_SDIO:
            reason_name = "SDIO";
            reason_desc = "Reset triggered by SDIO subsystem.";
            is_warning = true;
            break;
        case ESP_RST_USB:
            reason_name = "USB";
            reason_desc = "Reset triggered by USB subsystem.";
            is_warning = true;
            break;
        case ESP_RST_JTAG:
            reason_name = "JTAG";
            reason_desc = "Reset triggered via JTAG.";
            is_warning = true;
            break;
        case ESP_RST_EFUSE:
            reason_name = "EFUSE";
            reason_desc = "eFuse related reset.";
            is_error = true;
            break;
        case ESP_RST_PWR_GLITCH:
            reason_name = "PWR_GLITCH";
            reason_desc = "Power glitch detected.";
            is_error = true;
            break;
        case ESP_RST_CPU_LOCKUP:
            reason_name = "CPU_LOCKUP";
            reason_desc = "CPU lockup detected.";
            is_error = true;
            break;
        default:
            is_warning = true;
            break;
    }

    if(is_error) {
        ESP_LOGE(TAG, "================ RESET REASON ================");
        ESP_LOGE(TAG, " reason : %s (%d)", reason_name, reason);
        ESP_LOGE(TAG, " detail : %s", reason_desc);
        ESP_LOGE(TAG, "==============================================");
    } else if(is_warning) {
        ESP_LOGW(TAG, "================ RESET REASON ================");
        ESP_LOGW(TAG, " reason : %s (%d)", reason_name, reason);
        ESP_LOGW(TAG, " detail : %s", reason_desc);
        ESP_LOGW(TAG, "==============================================");
    } else {
        ESP_LOGI(TAG, "================ RESET REASON ================");
        ESP_LOGI(TAG, " reason : %s (%d)", reason_name, reason);
        ESP_LOGI(TAG, " detail : %s", reason_desc);
        ESP_LOGI(TAG, "==============================================");
    }
}

/* * Background task to handle system-level commands asynchronously.
 * Receives messages from BLE receiver or TCP client.
 */
static void sys_cmd_task(void* arg) {
    sys_cmd_t msg;

    ESP_LOGI("SYS_TASK", "System Command Task Started.");

    while(1) {
        if(xQueueReceive(sys_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch(msg) {
                case UPLOAD:
                    ESP_LOGD("SYS_TASK", ">>> [UPLOAD] Command Received!");
                    // Stop playback and turn LEDs green to indicate update mode
                    if(Player::getInstance().getState() != 1)
                        Player::getInstance().stop();
                    Player::getInstance().test(0, 128, 0);

                    // Trigger the background TCP OTA update task
                    tcp_client_start_update_task();
                    break;

                case RESET:
                    ESP_LOGD("SYS_TASK", ">>> [RESET] Command Received! Rebooting in 1s...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ESP_LOGI("SYS_TASK", "System will reboot, start flushing logs...");
                    vTaskDelay(pdMS_TO_TICKS(100));  // Give time for log to be written to buffer
                    sd_log_flush();
                    esp_restart();
                    break;

                case UPLOAD_SUCCESS:
                    ESP_LOGD("SYS_TASK", ">>> [RESET] Download Completed! Rebooting in 1s...");
                    Player::getInstance().stop();  // Turn off LEDs before reboot
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    // Reset after successful upload to apply new content
                    {
                        sys_cmd_t reset_cmd = RESET;
                        xQueueSend(sys_cmd_queue, &reset_cmd, 0);
                    }

                    break;

                default:
                    break;
            }
        }
    }
}

/* * Main application initialization task.
 * Sets up file systems, hardware configs, player, and communication modules.
 */
static void app_task(void* arg) {
    ESP_LOGI(TAG, "app_task start, HWM=%u", uxTaskGetStackHighWaterMark(NULL));

    // 0. Mount SD Card
    esp_err_t err = mount_sdcard();
    if(err == ESP_OK) {
        sd_mounted = true;
        ESP_LOGI(TAG, "SD card mount success");
    } else {
        sd_mounted = false;
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

#if LD_CFG_ENABLE_LOGGER
    // 1. Initialize SD Logger (Optional)
    err = sd_log_init();
    if(err == ESP_OK) {
        logger_inited = true;
        ESP_LOGI(TAG, "SD Logger success");
    } else {
        logger_inited = false;
        ESP_LOGE(TAG, "SD Logger init failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

#endif

#if LD_CFG_ENABLE_PT
    // 2. Initialize SD Card and frame reading system
    err = frame_system_init("0:/control.dat", "0:/frame.dat");
    ESP_LOGI(TAG, "frame_system_init=%s", esp_err_to_name(err));
    ESP_LOGD(TAG, "HWM after frame_system_init=%u", uxTaskGetStackHighWaterMark(NULL));

    if(err != ESP_OK) {
        frame_inited = false;
        // vTaskDelay(portMAX_DELAY);  // Halt task if critical files are missing
        frame_sys_ready = false;
        ESP_LOGE(TAG, "frame system init failed");
    } else {
        frame_inited = true;
        frame_sys_ready = true;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

#endif

    print_restart_reason();

    // 3. Pre-calculate Gamma Lookup Table for LED color correction
    calc_gamma_lut();

    // 4. Hardware Configuration (Temporary mapping for LED strips and I2C channels)
    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        ch_info.rmt_strips[i] = LD_BOARD_WS2812B_MAX_PIXEL_NUM;
    }
    for(int i = 0; i < LD_BOARD_PCA9955B_CH_NUM; i++) {
        ch_info.i2c_leds[i] = 1;
    }

    // 5. Initialize the core Player state machine
    Player::getInstance().init();

    vTaskDelay(pdMS_TO_TICKS(100));

    // 6. Create System Command Queue and spawn its handler task
    sys_cmd_queue = xQueueCreate(10, sizeof(sys_cmd_t));
    // sd_log_flush();
    if(sys_cmd_queue != NULL) {
        xTaskCreate(sys_cmd_task, "sys_cmd_task", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create sys_cmd_queue!");
    }

#if LD_CFG_ENABLE_BT
    // 7. Initialize NVS and Bluetooth Receiver
    nvs_flash_init();

    // Read assigned Player ID from SD card (fallback to 1 for testing)
    int player_id;
#if LD_CFG_ENABLE_PT
    player_id = get_sd_card_id();
#endif

    if(player_id == 0)
        ESP_LOGW(TAG, "get_sd_card_id() return 0.");

    // Configure and start the BLE Receiver
    bt_receiver_config_t rx_cfg = {
        .feedback_gpio_num = -1,
        .manufacturer_id = 0xFFFF,
        .my_player_id = player_id,
        .sync_window_us = 500000,
        .queue_size = 20,
    };
    bt_receiver_init(&rx_cfg);
    bt_receiver_start();

    vTaskDelay(pdMS_TO_TICKS(100));
#else
    // Fallback to console testing if BT is disabled
    console_test();
#endif

    // Indicate the initialization is completed.
    Player::getInstance().test(0, 0, 128);
    vTaskDelay(pdMS_TO_TICKS(500));
    Player::getInstance().stop();

    // Initialization complete, delete setup task to free memory
    vTaskDelete(NULL);
}

/* ESP-IDF Entry Point */
extern "C" void app_main(void) {
    xTaskCreate(app_task, "app_task", 16384, NULL, 5, NULL);
}
