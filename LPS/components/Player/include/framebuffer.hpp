#pragma once

#include "esp_err.h"

#include "ld_board.h"
#include "ld_config.h"
#include "ld_frame.h"
#include "ld_led_ops.h"
#include "ld_led_types.h"

#include "player_protocal.h"

enum class FbTestMode : uint8_t {
    OFF = 0,
    SOLID,
    BREATH,
};

enum class FbComputeStatus : uint8_t {
    OK = 0,
    HOLD,
    EOF_REACHED,
    ERROR_GENERAL, 
    ERROR_CRITICAL
};

class FrameBuffer {
  public:
    FrameBuffer();
    ~FrameBuffer();

    esp_err_t init();
    esp_err_t reset();
    esp_err_t deinit();

    FbComputeStatus compute(uint64_t time_ms);

    void set_test_mode(FbTestMode mode);
    FbTestMode get_test_mode() const;

    void set_test_color(grb8_t color);
    grb8_t get_test_color() const;

    void fill(grb8_t color);

    void print_buffer();
    frame_data* get_buffer();

  private:
    FbComputeStatus handle_frames(uint64_t time_ms);
    void lerp(uint8_t p);
    void gamma_correction();
    void brightness_correction();

    table_frame_t frame0{}, frame1{};

    bool is_frame_system_init = true;
    table_frame_t* current;
    table_frame_t* next;

    frame_data buffer;

    FbTestMode test_mode_ = FbTestMode::OFF;
    grb8_t test_color_ = {0, 0, 0};
    bool eof_reported_ = false;

    grb8_t make_breath_color(uint64_t time_ms) const;
};

void test_read_frame(table_frame_t* p);

void print_table_frame(const table_frame_t& frame);
void print_frame_data(const frame_data& data);
