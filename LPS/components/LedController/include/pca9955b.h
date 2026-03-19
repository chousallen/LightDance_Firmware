#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "ld_board.h"
#include "ld_led_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t command_byte;
    union {
        uint8_t data[LD_BOARD_PCA9955B_BUFFER_DATA_LEN]; /*!< Raw access to channel color payload */
        struct {
            uint8_t ch[LD_BOARD_PCA9955B_RGB_PER_IC][LD_BOARD_PCA9955B_COLOR_BYTES_PER_PIXEL]; /*!< logical mapping: RGB channels per device */
        };
    };
} pca9955b_buffer_t;

typedef struct {
    i2c_master_dev_handle_t i2c_dev_handle; /*!< I2C bus device handle */
    uint8_t i2c_addr;                       /*!< 7-bit I2C device address */

    pca9955b_buffer_t buffer; /*!< PWM register + LED color buffer */

    bool need_reset_IREF; /*!< Set true if IREF register needs to be reinitialized */
    bool iref_loss;
} pca9955b_dev_t;

/* Bus lifecycle */
esp_err_t i2c_bus_init(gpio_num_t i2c_gpio_sda, gpio_num_t i2c_gpio_scl, i2c_master_bus_handle_t* ret_i2c_bus_handle);

/* Device lifecycle */
esp_err_t pca9955b_init(pca9955b_dev_t* pca9955b, uint8_t i2c_addr, i2c_master_bus_handle_t i2c_bus_handle);
esp_err_t pca9955b_del(pca9955b_dev_t* pca9955b);

/* Buffer update */
esp_err_t pca9955b_set_pixel(pca9955b_dev_t* pca9955b, uint8_t pixel_idx, grb8_t color);
esp_err_t pca9955b_write_grb(pca9955b_dev_t* pca9955b, const grb8_t* colors, uint8_t count);
esp_err_t pca9955b_fill(pca9955b_dev_t* pca9955b, grb8_t color);

/* Transmission */
esp_err_t pca9955b_show(pca9955b_dev_t* pca9955b);

bool pca9955b_check_iref(pca9955b_dev_t* pca9955b);

#ifdef __cplusplus
}
#endif
