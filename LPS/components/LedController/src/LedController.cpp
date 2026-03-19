#include "LedController.hpp"

#include "string.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ld_board.h"
#include "ld_config.h"

static const char* TAG = "LedController";

bool pca_enable[LD_BOARD_PCA9955B_NUM] = {0};

LedController::LedController() {}

LedController::~LedController() {}

esp_err_t LedController::init() {
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Initializing LedController (WS2812B=%d, PCA9955B=%d)", LD_BOARD_WS2812B_NUM, LD_BOARD_PCA9955B_NUM);

    // 1. Input Validation
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(LD_BOARD_I2C_SDA_GPIO), ESP_ERR_INVALID_ARG, TAG, "Invalid SDA GPIO");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(LD_BOARD_I2C_SCL_GPIO), ESP_ERR_INVALID_ARG, TAG, "Invalid SCL GPIO");

    // 2. Initialize output handles to 0
    memset(ws2812b_devs, 0, sizeof(ws2812b_devs));
    memset(pca9955b_devs, 0, sizeof(pca9955b_devs));
    bus_handle = NULL;
    ESP_LOGD(TAG, "Device handles cleared");

    // 3. Initialize I2C Bus
    ESP_LOGD(TAG, "Initializing I2C bus");
    ESP_GOTO_ON_ERROR(i2c_bus_init(LD_BOARD_I2C_SDA_GPIO, LD_BOARD_I2C_SCL_GPIO, &bus_handle), err, TAG, "Failed to initialize I2C bus");
    ESP_LOGD(TAG, "I2C bus ready");

    // 4. Initialize WS2812B Strips
    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        ESP_LOGD(TAG, "Init WS2812B[%d] gpio=%d pixels=%d", i, BOARD_HW_CONFIG.rmt_pins[i], ch_info.rmt_strips[i]);
#if LD_CFG_IGNORE_DRIVER_INIT_FAIL
        esp_err_t ws_ret = ws2812b_init(&ws2812b_devs[i], BOARD_HW_CONFIG.rmt_pins[i], ch_info.rmt_strips[i]);
        if(ws_ret != ESP_OK) {
            ESP_LOGW(TAG, "Ignore init fail WS2812B[%d]: %s", i, esp_err_to_name(ws_ret));
        }
#else
        ESP_GOTO_ON_ERROR(ws2812b_init(&ws2812b_devs[i], BOARD_HW_CONFIG.rmt_pins[i], ch_info.rmt_strips[i]), err, TAG, "Failed to init WS2812B[%d]", i);
#endif
    }

    // 5. Initialize PCA9955B Chips
    for(int i = 0; i < LD_BOARD_PCA9955B_NUM; i++) {
        ESP_LOGD(TAG, "Init PCA9955B[%d] addr=0x%02x", i, BOARD_HW_CONFIG.i2c_addrs[i]);
#if LD_CFG_IGNORE_DRIVER_INIT_FAIL
        esp_err_t pca_ret = pca9955b_init(&pca9955b_devs[i], BOARD_HW_CONFIG.i2c_addrs[i], bus_handle);
        if(pca_ret == ESP_OK) {
            pca_enable[i] = true;
        } else {
            ESP_LOGW(TAG, "Ignore init fail PCA9955B[%d] addr=0x%02x: %s", i, BOARD_HW_CONFIG.i2c_addrs[i], esp_err_to_name(pca_ret));
        }
#else
        esp_err_t probe_ret = i2c_master_probe(bus_handle, BOARD_HW_CONFIG.i2c_addrs[i], LD_BOARD_I2C_PROBE_TIMEOUT_MS);
        if(probe_ret == ESP_OK) {
            pca_enable[i] = true;
            ESP_LOGD(TAG, "Detected PCA9955B[%d] addr=0x%02x", i, BOARD_HW_CONFIG.i2c_addrs[i]);
            ESP_GOTO_ON_ERROR(pca9955b_init(&pca9955b_devs[i], BOARD_HW_CONFIG.i2c_addrs[i], bus_handle), err, TAG, "Failed to init PCA9955B[%d]", i);
        } else {
            ESP_LOGE(TAG, "Fail to find device at address 0x%02x", BOARD_HW_CONFIG.i2c_addrs[i]);
            ret = probe_ret;
            goto err;
        }
#endif
    }

    ESP_LOGI(TAG, "LedController initialized successfully");
    return ESP_OK;

err:
    // If init failed, cleanup whatever was allocated
    ESP_LOGE(TAG, "LedController init failed: %s", esp_err_to_name(ret));
    deinit();
    return ret;
}

esp_err_t LedController::write_channel(int ch_idx, const grb8_t* data) {
    // 1. Validate Input
    ESP_RETURN_ON_FALSE(data, ESP_ERR_INVALID_ARG, TAG, "Data buffer is NULL");

    // 2. Handle PCA9955B Strips
    if(ch_idx < LD_BOARD_PCA9955B_CH_NUM) {

        // Calculate device and pixel index (LD_BOARD_PCA9955B_RGB_PER_IC LEDs per PCA9955B chip)
        int dev_idx = ch_idx / LD_BOARD_PCA9955B_RGB_PER_IC;
        int pixel_idx = ch_idx % LD_BOARD_PCA9955B_RGB_PER_IC;

        // Validate PCA device bounds
        if(dev_idx >= LD_BOARD_PCA9955B_NUM) {
            ESP_LOGW(TAG, "Channel index %d out of range (Max PCA dev: %d)", ch_idx, LD_BOARD_PCA9955B_NUM - 1);
            return ESP_ERR_INVALID_ARG;
        }

        // Ensure the device handle is valid
        ESP_RETURN_ON_FALSE(&pca9955b_devs[dev_idx], ESP_ERR_INVALID_STATE, TAG, "PCA9955B[%d] not initialized", dev_idx);

        return pca9955b_set_pixel(&pca9955b_devs[dev_idx], pixel_idx, data[0]);
    }

    // 3. Handle WS2812B Strips
    if(ch_idx >= LD_BOARD_PCA9955B_CH_NUM) {
        int ws_idx = ch_idx - LD_BOARD_PCA9955B_CH_NUM;
        ESP_RETURN_ON_FALSE(ws_idx >= 0 && ws_idx < LD_BOARD_WS2812B_NUM, ESP_ERR_INVALID_ARG, TAG, "WS2812B channel out of range: %d", ch_idx);

        // Ensure the device handle is valid before writing
        ESP_RETURN_ON_FALSE(&ws2812b_devs[ws_idx], ESP_ERR_INVALID_STATE, TAG, "WS2812B[%d] not initialized", ws_idx);

        // Pass the full strip buffer to the HAL
        return ws2812b_write_grb(&ws2812b_devs[ws_idx], data, ws2812b_devs[ws_idx].pixel_num);
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t LedController::write_frame(const frame_data* frame) {
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_INVALID_ARG, TAG, "frame is NULL");
    ESP_LOGD(TAG, "write_frame start");

    for(int i = 0; i < LD_BOARD_PCA9955B_CH_NUM; ++i) {
        ESP_RETURN_ON_ERROR(write_channel(i, &frame->pca9955b[i]), TAG, "write PCA ch %d failed", i);
    }

    for(int i = 0; i < LD_BOARD_WS2812B_NUM; ++i) {
        ESP_RETURN_ON_ERROR(write_channel(i + LD_BOARD_PCA9955B_CH_NUM, frame->ws2812b[i]), TAG, "write WS ch %d failed", i);
    }

    ESP_LOGD(TAG, "write_frame complete");
    return ESP_OK;
}

esp_err_t LedController::show() {
    esp_err_t ret = ESP_OK;
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "show() start");

#if LD_CFG_SHOW_TIME_PER_FRAME
    uint64_t start = esp_timer_get_time();
#endif

    // 1. Trigger WS2812B transmission (Asynchronous/Non-blocking)
    ESP_LOGD(TAG, "show() phase 1: trigger WS2812B [0..%d)", LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE);
    for(int i = 0; i < LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE; i++) {
        err = ws2812b_show(&ws2812b_devs[i]);
        if(err != ESP_OK) {
            // Log error but continue to try updating other LEDs
            ESP_LOGE(TAG, "Failed to show WS2812B[%d]: %s", i, esp_err_to_name(err));
            ret = err;  // Latch the error code
        }
    }

    // 3. Wait for WS2812B transmission to complete
    ESP_LOGD(TAG, "show() phase 2: wait WS2812B [0..%d)", LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE);
    for(int i = 0; i < LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE; i++) {
        err = ws2812b_wait_done(&ws2812b_devs[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Wait done failed for WS2812B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

    // 2. Trigger PCA9955B transmission (Synchronous/Blocking)
    ESP_LOGD(TAG, "show() phase 3: flush PCA9955B [0..%d)", LD_BOARD_LED_SHOW_PCA_FIRST_BATCH_SIZE);
    for(int i = 0; i < LD_BOARD_LED_SHOW_PCA_FIRST_BATCH_SIZE; i++) {
        if(!pca_enable[i]) {
            ESP_LOGD(TAG, "Skip PCA9955B[%d] (disabled)", i);
            continue;
        }
        if(!pca9955b_devs[i].iref_loss && !pca9955b_check_iref(&pca9955b_devs[i])) {
            ESP_LOGE(TAG, "IREF check failed for PCA9955B[%d]", i);
        }
        err = pca9955b_show(&pca9955b_devs[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to show PCA9955B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

    // 1. Trigger WS2812B transmission (Asynchronous/Non-blocking)
    ESP_LOGD(TAG, "show() phase 4: trigger WS2812B [%d..%d)", LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE, LD_BOARD_WS2812B_NUM);
    for(int i = LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE; i < LD_BOARD_WS2812B_NUM; i++) {
        err = ws2812b_show(&ws2812b_devs[i]);
        if(err != ESP_OK) {
            // Log error but continue to try updating other LEDs
            ESP_LOGE(TAG, "Failed to show WS2812B[%d]: %s", i, esp_err_to_name(err));
            ret = err;  // Latch the error code
        }
    }

    // 3. Wait for WS2812B transmission to complete
    ESP_LOGD(TAG, "show() phase 5: wait WS2812B [%d..%d)", LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE, LD_BOARD_WS2812B_NUM);
    for(int i = LD_BOARD_LED_SHOW_WS_FIRST_BATCH_SIZE; i < LD_BOARD_WS2812B_NUM; i++) {
        err = ws2812b_wait_done(&ws2812b_devs[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Wait done failed for WS2812B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

    // 2. Trigger PCA9955B transmission (Synchronous/Blocking)
    ESP_LOGD(TAG, "show() phase 6: flush PCA9955B [%d..%d)", LD_BOARD_LED_SHOW_PCA_FIRST_BATCH_SIZE, LD_BOARD_PCA9955B_NUM);
    for(int i = LD_BOARD_LED_SHOW_PCA_FIRST_BATCH_SIZE; i < LD_BOARD_PCA9955B_NUM; i++) {
        if(!pca_enable[i]) {
            ESP_LOGD(TAG, "Skip PCA9955B[%d] (disabled)", i);
            continue;
        }
        if(!pca9955b_devs[i].iref_loss && !pca9955b_check_iref(&pca9955b_devs[i])) {
            ESP_LOGE(TAG, "IREF check failed for PCA9955B[%d]", i);
        }
        err = pca9955b_show(&pca9955b_devs[i]);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to show PCA9955B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

#if LD_CFG_SHOW_TIME_PER_FRAME
    uint64_t end = esp_timer_get_time();
    ESP_LOGD(TAG, "show() execution time: %llu us", (end - start));
#endif

    if(ret == ESP_OK) {
        ESP_LOGD(TAG, "show() complete");
    } else {
        ESP_LOGW(TAG, "show() complete with error: %s", esp_err_to_name(ret));
    }

    // Return the last error encountered, or ESP_OK if all went well
    return ret;
}

esp_err_t LedController::deinit() {
    ESP_LOGI(TAG, "De-initializing LED Controller...");

    // 1. Free WS2812B Devices
    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        ESP_RETURN_ON_ERROR(ws2812b_del(&ws2812b_devs[i]), TAG, "Failed to delete WS2812B[%d]", i);
    }

    // 2. Free PCA9955B Devices
    for(int i = 0; i < LD_BOARD_PCA9955B_NUM; i++) {
        if(!pca_enable[i]) {
            continue;
        }
        ESP_RETURN_ON_ERROR(pca9955b_del(&pca9955b_devs[i]), TAG, "Failed to delete PCA9955B[%d]", i);
    }

    // 3. Free I2C Bus
    if(bus_handle != NULL) {
        esp_err_t err = i2c_del_master_bus(bus_handle);
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete I2C bus: %s", esp_err_to_name(err));
        }
        bus_handle = NULL;  // Prevent double-free if deinit is called again
    }

    ESP_LOGI(TAG, "De-initialization complete");
    return ESP_OK;
}

esp_err_t LedController::fill(grb8_t color) {
    esp_err_t ret = ESP_OK;
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "fill() color=(r:%u g:%u b:%u)", color.r, color.g, color.b);

    // 1. Fill WS2812B Strips
    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        // Ensure the device handle is valid before operation
        err = ws2812b_fill(&ws2812b_devs[i], color);

        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to fill WS2812B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

    // 2. Fill PCA9955B Chips
    for(int i = 0; i < LD_BOARD_PCA9955B_NUM; i++) {
        // Ensure the device handle is valid
        err = pca9955b_fill(&pca9955b_devs[i], color);

        if(err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to fill PCA9955B[%d]: %s", i, esp_err_to_name(err));
            ret = err;
        }
    }

    // Return ESP_OK only if all devices succeeded, otherwise return the last error code
    if(ret == ESP_OK) {
        ESP_LOGD(TAG, "fill() complete");
    } else {
        ESP_LOGW(TAG, "fill() complete with error: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t LedController::black_out() {
    // 1. Clear internal buffers to Black (0, 0, 0)
    ESP_LOGI(TAG, "black_out() start");

    esp_err_t ret = fill(GRB_BLACK);

    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "Blackout fill incomplete: %s", esp_err_to_name(ret));
    }

    // 2. Flush changes to hardware immediately
    esp_err_t ret_show = show();
    if(ret_show != ESP_OK) {
        ESP_LOGW(TAG, "black_out() show failed: %s", esp_err_to_name(ret_show));
    }

    // 3. Return the first error encountered
    if(ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "black_out() complete");
    return ret_show;
}

void LedController::print_buffer() {
    ESP_LOGD(TAG, "print_buffer() dump %d WS2812B strips", LD_BOARD_WS2812B_NUM);
    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        ws2812b_print_buffer(&ws2812b_devs[i]);
    }
}
