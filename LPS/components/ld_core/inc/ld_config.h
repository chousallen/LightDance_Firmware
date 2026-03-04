#pragma once

/**
 * @file ld_config.h
 * @brief Project-wide compile-time switches and runtime limits.
 *
 * This header is intentionally lightweight and shared by multiple components.
 * Keep values deterministic and avoid side effects in macro definitions.
 */

/* Feature toggles */
#define LD_CFG_ENABLE_SD 1
#define LD_CFG_ENABLE_BT 0
#define LD_CFG_ENABLE_LOGGER 0

/* Per-channel max brightness for PCA9955B path (0..255). */
#define LD_CFG_PCA9955B_MAX_BRIGHTNESS_R 210
#define LD_CFG_PCA9955B_MAX_BRIGHTNESS_G 200
#define LD_CFG_PCA9955B_MAX_BRIGHTNESS_B 255

/* Global max brightness for WS2812B path (0..255). */
#define LD_CFG_WS2812B_MAX_BRIGHTNESS 50

/* I2C configuration */
#define LD_CFG_I2C_FREQ_HZ 400000
#define LD_CFG_I2C_TIMEOUT_MS 2

/* RMT configuration */
#define LD_CFG_RMT_TIMEOUT_MS 10

/* Player runtime/task tuning */
#define LD_CFG_PLAYER_TASK_NAME "PlayerTask"
#define LD_CFG_PLAYER_TASK_STACK_SIZE 8192
#define LD_CFG_PLAYER_TASK_PRIORITY 5
#define LD_CFG_PLAYER_TASK_CORE_ID 1
#define LD_CFG_PLAYER_EVENT_QUEUE_LEN 50
#define LD_CFG_PLAYER_FPS 40
#define LD_CFG_PLAYER_BOOT_RELOAD_DELAY_MS 100
#define LD_CFG_PLAYER_RETRY_DELAY_MS 1000
#define LD_CFG_PLAYER_TEST_BREATH_CYCLE_MS 6000
#define LD_CFG_PLAYER_TEST_FRAME_INTERVAL_MS 2000
#define LD_CFG_PLAYER_DEBUG_DUMP_PIXELS 5
#define LD_CFG_PLAYER_GPTIMER_RESOLUTION_HZ 1000000

/* Behavior controls */
#define LD_CFG_IGNORE_DRIVER_INIT_FAIL 1
#define LD_CFG_SHOW_TIME_PER_FRAME 0

#define LD_CFG_ENABLE_INTERNAL_PULLUP 1
