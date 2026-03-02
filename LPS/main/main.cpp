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
#include "tcp_client.h"

static const char* TAG = "APP";

// System state flags and global queue
static bool frame_sys_ready = false;
QueueHandle_t sys_cmd_queue = NULL;

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
                    Player::getInstance().test(0, 255, 0);

                    // Trigger the background TCP OTA update task
                    tcp_client_start_update_task();
                    break;

                case RESET:
                    ESP_LOGD("SYS_TASK", ">>> [RESET] Command Received! Rebooting in 1s...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                    break;

                case UPLOAD_SUCCESS:
                    ESP_LOGD("SYS_TASK", ">>> [RESET] Download Completed! Rebooting in 1s...");
                    Player::getInstance().stop();  // Turn off LEDs before reboot
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();  // Reboot to apply new files and restore clean memory state
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

#if LD_CFG_ENABLE_SD
    // 1. Initialize SD Card and frame reading system
    esp_err_t sd_err = frame_system_init("0:/control.dat", "0:/frame.dat");
    ESP_LOGI(TAG, "frame_system_init=%s", esp_err_to_name(sd_err));
    ESP_LOGI(TAG, "HWM after frame_system_init=%u", uxTaskGetStackHighWaterMark(NULL));

    vTaskDelay(pdMS_TO_TICKS(1000));

    if(sd_err != ESP_OK) {

        // vTaskDelay(portMAX_DELAY); // Halt task if critical files are missing
        frame_sys_ready = false;
        ESP_LOGE(TAG, "frame system init failed, halt");
    } else {
        frame_sys_ready = true;

#if LD_CFG_ENABLE_LOGGER
        // 2. Initialize SD Logger (Optional)
        esp_err_t log_err = sd_logger_init("/sd/LOGGER.log");
        if(log_err != ESP_OK) {
            ESP_LOGE(TAG, "SD Logger init failed: %s", esp_err_to_name(log_err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
#endif

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
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 6. Create System Command Queue and spawn its handler task
    sys_cmd_queue = xQueueCreate(10, sizeof(sys_cmd_t));
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
#if LD_CFG_ENABLE_SD
    player_id = get_sd_card_id();
#else
    player_id = 1;
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
#else
    // Fallback to console testing if BT is disabled
    console_test();
#endif

    // Initialization complete, delete setup task to free memory
    vTaskDelete(NULL);
}

/* ESP-IDF Entry Point */
extern "C" void app_main(void) {
    xTaskCreate(app_task, "app_task", 16384, NULL, 5, NULL);
}