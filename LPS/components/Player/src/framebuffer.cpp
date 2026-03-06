#include "framebuffer.hpp"

#include <string.h>
#include "algorithm"
#include "esp_log.h"
#include "readframe.h"

static const char* TAG = "fb";

static int count = 0;

// p = 0..255
static inline uint8_t calc_lerp_p(uint64_t time_ms, const uint64_t t1, const uint64_t t2) {

    if(t2 <= t1)
        return 255;
    if(time_ms >= t2)
        return 255;

    const uint64_t dt = time_ms - t1;
    const uint64_t dur = t2 - t1;
    return (uint8_t)((dt * 255) / dur);
}

FrameBuffer::FrameBuffer() {
    current = &frame0;
    next = &frame1;
}
FrameBuffer::~FrameBuffer() {}

esp_err_t FrameBuffer::init() {
    
    test_mode_ = FbTestMode::OFF;
    eof_reported_ = false;

    current = &frame0;
    next = &frame1;

    memset(&frame0, 0, sizeof(frame0));
    memset(&frame1, 0, sizeof(frame1));
    memset(&buffer, 0, sizeof(buffer));

    count = 0;
#if LD_CFG_ENABLE_SD
    read_frame(current);
    // print_table_frame(*current);

    read_frame(next);
    // print_table_frame(*next);

#else
    test_read_frame(current);
    test_read_frame(next);
#endif

    return ESP_OK;
}

esp_err_t FrameBuffer::reset() {
    test_mode_ = FbTestMode::OFF;
    eof_reported_ = false;

    current = &frame0;
    next = &frame1;

    memset(&frame0, 0, sizeof(frame0));
    memset(&frame1, 0, sizeof(frame1));
    memset(&buffer, 0, sizeof(buffer));

#if LD_CFG_ENABLE_SD
    frame_reset();

    read_frame(current);
    // print_table_frame(*current);
    read_frame(next);
    // print_table_frame(*next);

#else
    count = 0;
    test_read_frame(current);
    test_read_frame(next);
#endif

    return ESP_OK;
}

esp_err_t FrameBuffer::deinit() {
    return ESP_OK;
}

void FrameBuffer::set_test_mode(FbTestMode mode) {
    test_mode_ = mode;
}

FbTestMode FrameBuffer::get_test_mode() const {
    return test_mode_;
}

void FrameBuffer::set_test_color(grb8_t color) {
    test_color_ = color;
}

grb8_t FrameBuffer::make_breath_color(uint64_t time_ms) const {
    const uint32_t cycle_ms = (LD_CFG_PLAYER_TEST_BREATH_CYCLE_MS > 0) ? LD_CFG_PLAYER_TEST_BREATH_CYCLE_MS : 1;
    const uint64_t phase_ms = time_ms % cycle_ms;
    uint16_t h_cal = (uint16_t)((phase_ms * 1536ULL) / cycle_ms);
    if(h_cal > 1535) {
        h_cal = 1535;
    }
    return hsv_to_grb_u8(hsv8(h_cal, 255, 255));
}

grb8_t FrameBuffer::get_test_color() const {
    return test_color_;
}

FbComputeStatus FrameBuffer::compute(uint64_t time_ms) {

    // ---- Test path ----
    if(test_mode_ != FbTestMode::OFF) {
        grb8_t c = test_color_;
        if(test_mode_ == FbTestMode::BREATH) {
            c = make_breath_color(time_ms);
        }
        fill(c);
        gamma_correction();
        brightness_correction();

        return FbComputeStatus::OK;
    }

    // ---- Normal path ----
    FbComputeStatus status = handle_frames(time_ms);
    if(status == FbComputeStatus::ERROR_GENERAL || status == FbComputeStatus::ERROR_CRITICAL) {
        return status;
    }

    if(status == FbComputeStatus::OK) {
        uint8_t p = (current->fade) ? calc_lerp_p(time_ms, current->timestamp, next->timestamp) : 0;

        lerp(p);
    }

    gamma_correction();
    brightness_correction();

    return status;
}

void FrameBuffer::fill(grb8_t color) {
    for(int ch = 0; ch < LD_BOARD_WS2812B_NUM; ch++) {
        for(int i = 0; i < LD_BOARD_WS2812B_MAX_PIXEL_NUM; i++) {
            buffer.ws2812b[ch][i] = color;
        }
    }

    for(int ch = 0; ch < LD_BOARD_PCA9955B_CH_NUM; ch++) {
        buffer.pca9955b[ch] = color;
    }

    return;
}

FbComputeStatus FrameBuffer::handle_frames(uint64_t time_ms) {
    if(current == nullptr || next == nullptr) {
        ESP_LOGE(TAG, "FrameBuffer not initialized");
        return FbComputeStatus::ERROR_GENERAL;
    }

    if(time_ms < current->timestamp) {
        buffer = current->data;
        return FbComputeStatus::HOLD;
    }

    while(time_ms >= next->timestamp) {
        std::swap(current, next);

#if LD_CFG_ENABLE_SD
        esp_err_t err = read_frame(next);
        if(err == ESP_ERR_NOT_FOUND) {
            buffer = current->data;
            if(!eof_reported_) {
                eof_reported_ = true;
                ESP_LOGI(TAG, "end");
                return FbComputeStatus::EOF_REACHED;
            }
            return FbComputeStatus::HOLD;
        }
        if(err == ESP_ERR_INVALID_SIZE) {
            ESP_LOGE(TAG, "read_frame failed: %s", esp_err_to_name(err));
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
        if(err == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG, "read_frame failed: %s", esp_err_to_name(err));
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
        if(err == ESP_ERR_INVALID_CRC) {
            ESP_LOGE(TAG, "read_frame failed: %s", esp_err_to_name(err));
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
        if(err == ESP_FAIL) {
            ESP_LOGE(TAG, "read_frame failed: %s", esp_err_to_name(err));
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
        if(err == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "read_frame failed: %s", esp_err_to_name(err));
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
        // print_table_frame(*next);
#else
        test_read_frame(next);
#endif

        if(next->timestamp <= current->timestamp) {
            ESP_LOGE(TAG, "Non-monotonic timestamp: current=%" PRIu64 ", next=%" PRIu64, current->timestamp, next->timestamp);
            buffer = current->data;
            return FbComputeStatus::ERROR_GENERAL;
        }
    }

    return FbComputeStatus::OK;
}

void FrameBuffer::lerp(uint8_t p) {
    for(int ch = 0; ch < LD_BOARD_WS2812B_NUM; ch++) {
        for(int i = 0; i < LD_BOARD_WS2812B_MAX_PIXEL_NUM; i++) {
            buffer.ws2812b[ch][i] = grb_lerp_hsv_u8(current->data.ws2812b[ch][i], next->data.ws2812b[ch][i], p);
        }
    }

    for(int ch = 0; ch < LD_BOARD_PCA9955B_CH_NUM; ch++) {
        buffer.pca9955b[ch] = grb_lerp_hsv_u8(current->data.pca9955b[ch], next->data.pca9955b[ch], p);
    }
}

void FrameBuffer::gamma_correction() {
    for(int ch = 0; ch < LD_BOARD_WS2812B_NUM; ch++) {
        for(int i = 0; i < LD_BOARD_WS2812B_MAX_PIXEL_NUM; i++) {
            buffer.ws2812b[ch][i] = grb_gamma_u8(buffer.ws2812b[ch][i], LED_WS2812B);
        }
    }

    for(int ch = 0; ch < LD_BOARD_PCA9955B_CH_NUM; ch++) {
        buffer.pca9955b[ch] = grb_gamma_u8(buffer.pca9955b[ch], LED_PCA9955B);
    }
}

void FrameBuffer::brightness_correction() {
    for(int ch = 0; ch < LD_BOARD_WS2812B_NUM; ch++) {
        for(int i = 0; i < LD_BOARD_WS2812B_MAX_PIXEL_NUM; i++) {
            buffer.ws2812b[ch][i] = grb_set_brightness(buffer.ws2812b[ch][i], LED_WS2812B);
        }
    }

    for(int ch = 0; ch < LD_BOARD_PCA9955B_CH_NUM; ch++) {
        buffer.pca9955b[ch] = grb_set_brightness(buffer.pca9955b[ch], LED_PCA9955B);
    }
}

frame_data* FrameBuffer::get_buffer() {
    return &buffer;
}

void print_table_frame(const table_frame_t& frame) {
    ESP_LOGI(TAG, "=== table_frame_t ===");
    ESP_LOGI(TAG, "timestamp : %" PRIu64 " ms", frame.timestamp);
    ESP_LOGI(TAG, "fade      : %s", frame.fade ? "true" : "false");
    print_frame_data(frame.data);
    ESP_LOGI(TAG, "=====================");
}

void print_frame_data(const frame_data& data) {
    ESP_LOGI(TAG, "[WS2812]");
    for(int ch = 0; ch < LD_BOARD_WS2812B_NUM; ch++) {
        int len = ch_info.rmt_strips[ch];

        if(len < 0)
            len = 0;
        if(len > LD_BOARD_WS2812B_MAX_PIXEL_NUM)
            len = LD_BOARD_WS2812B_MAX_PIXEL_NUM;

        int dump = (len > LD_CFG_PLAYER_DEBUG_DUMP_PIXELS) ? LD_CFG_PLAYER_DEBUG_DUMP_PIXELS : len;

        ESP_LOGI(TAG, "  CH %d (len=%d):", ch, len);
        for(int i = 0; i < dump; i++) {
            const grb8_t& p = data.ws2812b[ch][i];
            ESP_LOGI(TAG, "    [%d] G=%u R=%u B=%u", i, p.g, p.r, p.b);
        }
        if(dump < len) {
            ESP_LOGI(TAG, "    ...");
        }
    }

    ESP_LOGI(TAG, "[PCA9955]");
    for(int i = 0; i < LD_BOARD_PCA9955B_CH_NUM; i++) {
        const grb8_t& p = data.pca9955b[i];
        ESP_LOGI(TAG, "  CH %2d: G=%u R=%u B=%u", i, p.g, p.r, p.b);
    }
}

void FrameBuffer::print_buffer() {
    print_frame_data(buffer);
}

static uint8_t brightness = 255;

static grb8_t red = {.g = 0, .r = brightness, .b = 0};
static grb8_t green = {.g = brightness, .r = 0, .b = 0};
static grb8_t blue = {.g = 0, .r = 0, .b = brightness};
static grb8_t color_pool[3] = {red, green, blue};

void test_read_frame(table_frame_t* p) {
    p->timestamp = (uint64_t)count * LD_CFG_PLAYER_TEST_FRAME_INTERVAL_MS;
    p->fade = true;
    for(int ch_idx = 0; ch_idx < LD_BOARD_WS2812B_NUM; ch_idx++) {
        for(int i = 0; i < ch_info.rmt_strips[ch_idx]; i++) {
            p->data.ws2812b[ch_idx][i] = grb_lerp_hsv_u8(color_pool[count % 3], color_pool[(count + 1) % 3], i * 255 / ch_info.rmt_strips[ch_idx]);
        }
    }
    for(int ch_idx = 0; ch_idx < LD_BOARD_PCA9955B_CH_NUM; ch_idx++) {
        if(ch_info.i2c_leds[ch_idx]) {
            p->data.pca9955b[ch_idx] = color_pool[count % 3];
        }
    }
    count++;
}
