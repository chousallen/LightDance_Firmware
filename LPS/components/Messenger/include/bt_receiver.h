#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "player.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bt_receiver_callback_t)(void);

// --- Received Packet ---
typedef struct {
    uint8_t cmd_id;
    uint8_t cmd_type;
    uint64_t target_mask;
    uint32_t delay_val;
    uint32_t prep_time;
    uint8_t data[3];
    int8_t rssi;
    int64_t rx_time_us;
    uint8_t mac[6];
} ble_rx_packet_t;

// --- cmd type ---
typedef enum {
    LPS_CMD_PLAY    = 0x01,
    LPS_CMD_PAUSE   = 0x02,
    LPS_CMD_STOP    = 0x03,
    LPS_CMD_RELEASE = 0x04,
    LPS_CMD_TEST    = 0x05,
    LPS_CMD_CANCEL  = 0x06,
    LPS_CMD_CHECK   = 0x07,
    LPS_CMD_UPLOAD  = 0x08,
    LPS_CMD_RESET   = 0x09
} lps_cmd_t;

typedef enum {
    UPLOAD = 0x08,
    RESET = 0x09,
    UPLOAD_SUCCESS = 1
} sys_cmd_t;

// --- Init Config ---
typedef struct {
    int feedback_gpio_num;      
    uint16_t manufacturer_id;   
    int my_player_id; 
    uint32_t sync_window_us;    
    uint32_t queue_size;        
} bt_receiver_config_t;
extern QueueHandle_t sys_cmd_queue;
esp_err_t bt_receiver_init(const bt_receiver_config_t *config);
esp_err_t bt_receiver_start(void);
esp_err_t bt_receiver_stop(void);
esp_err_t bt_receiver_deinit(void);

#ifdef __cplusplus
}
#endif
