#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "LedController.hpp"
#include "ld_gamma_lut.h"

#include "framebuffer.hpp"

grb8_t colors[3] = {
    grb8(255, 0, 0),
    grb8(0, 255, 0),
    grb8(0, 0, 255),
};

LedController controller;
FrameBuffer fb;

extern "C" void app_main(void) {
    calc_gamma_lut();

    for(int i = 0; i < LD_BOARD_WS2812B_NUM; i++) {
        ch_info.rmt_strips[i] = LD_BOARD_WS2812B_MAX_PIXEL_NUM;
    }
    for(int i = 0; i < LD_BOARD_PCA9955B_CH_NUM; i++) {
        ch_info.i2c_leds[i] = 1;
    }

    controller.init();

    fb.init();

    while(true) {
        fb.fill(grb8(255, 0, 0));
        controller.write_frame(fb.get_buffer());
        controller.show();
        vTaskDelay(pdMS_TO_TICKS(1000));

        fb.fill(grb8(0, 255, 0));
        controller.write_frame(fb.get_buffer());
        controller.show();
        vTaskDelay(pdMS_TO_TICKS(1000));

        fb.fill(grb8(0, 0, 255));
        controller.write_frame(fb.get_buffer());
        controller.show();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}