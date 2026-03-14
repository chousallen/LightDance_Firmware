# ld_core

Shared foundation layer for LightDance firmware.

`ld_core` centralizes reusable data types, board/channel configuration, and color-processing utilities used by runtime modules (`Player`, `LedController`, `PT_Reader`, and `main` startup code).

## Table of Contents

1. [What This Component Owns](#what-this-component-owns)
2. [Repository Layout](#repository-layout)
3. [Core Concepts](#core-concepts)
4. [Public API](#public-api)
5. [Initialization Contract](#initialization-contract)
6. [Usage Example](#usage-example)
7. [Build Integration](#build-integration)
8. [Maintenance Notes](#maintenance-notes)

## What This Component Owns

This component provides:
- LED color/value types (`grb8_t`, `hsv8_t`, `led_type_t`)
- Color conversion and interpolation (`GRB <-> HSV`, linear blend)
- Gamma lookup tables and brightness limiting
- Board-level hardware mapping (`BOARD_HW_CONFIG`)
- Runtime channel pixel metadata (`ch_info`)
- Shared frame payload structures for playback/rendering paths

This component does not provide:
- Hardware transport drivers (RMT/I2C send logic)
- Task scheduling or player control flow
- Experimental/dev-only helpers

## Repository Layout

```text
components/ld_core/
|-- inc/
|   |-- ld_config.h      # feature flags, brightness caps, timeouts
|   |-- ld_led_types.h   # color structs, enums, and common constants
|   |-- ld_math_u8.h     # 8-bit math helpers (lerp/scaling/min/max)
|   |-- ld_gamma_lut.h   # gamma constants + LUT declarations
|   |-- ld_led_ops.h     # color conversion/interpolation/output transforms
|   |-- ld_board.h       # board mapping + channel info structs
|   `-- ld_frame.h       # shared frame payload definitions
|-- src/
|   |-- ld_gamma_lut.c   # LUT generation implementation
|   `-- ld_board.c       # BOARD_HW_CONFIG and ch_info definitions
`-- CMakeLists.txt
```

## Core Concepts

### Color Model

- `grb8_t` stores channels as `g, r, b` (8-bit each).
- `hsv8_t` uses:
  - `h`: `0..1535` (`6 * 256` hue sectors)
  - `s`: `0..255`
  - `v`: `0..255`

### LED Device Types

`led_type_t` currently supports:
- `LED_WS2812B`
- `LED_PCA9955B`

Gamma and brightness behavior is selected by `led_type_t`.

### Board and Channel Model

- `BOARD_HW_CONFIG` is a global constant containing:
  - 8 PCA9955B I2C addresses
  - 8 WS2812B GPIO output pins
- `ch_info` is a mutable global containing pixel counts:
  - `rmt_strips[LD_BOARD_WS2812B_NUM]`
  - `i2c_leds[LD_BOARD_PCA9955B_CH_NUM]`

## Public API

All headers under `components/ld_core/inc` are treated as public.

### `ld_config.h`

Global compile-time flags and limits, including:
- `LD_CFG_ENABLE_PT`, `LD_CFG_ENABLE_BT`, `LD_CFG_ENABLE_LOGGER`
- `LD_CFG_PCA9955B_MAX_BRIGHTNESS_R/G/B`
- `LD_CFG_WS2812B_MAX_BRIGHTNESS`
- `LD_CFG_I2C_FREQ_HZ`, `LD_CFG_I2C_TIMEOUT_MS`, `LD_CFG_RMT_TIMEOUT_MS`
- `LD_CFG_IGNORE_DRIVER_INIT_FAIL`, `LD_CFG_ENABLE_INTERNAL_PULLUP`

### `ld_gamma_lut.h`

- Gamma constants for OF and LED paths:
  - `GAMMA_OF_R/G/B`
  - `GAMMA_LED_R/G/B`
- LUT buffers:
  - `GAMMA_OF_*_lut[256]`
  - `GAMMA_LED_*_lut[256]`
- Initializer:
  - `void calc_gamma_lut(void);`

### `ld_led_ops.h`

Key operations:
- Conversion:
  - `hsv8_t grb_to_hsv_u8(grb8_t in);`
  - `grb8_t hsv_to_grb_u8(hsv8_t in);`
- Interpolation:
  - `grb8_t grb_lerp_u8(grb8_t start, grb8_t end, uint8_t t);`
  - `grb8_t grb_lerp_hsv_u8(grb8_t start, grb8_t end, uint8_t t);`
- Output transforms:
  - `grb8_t grb_gamma_u8(grb8_t in, led_type_t type);`
  - `grb8_t grb_set_brightness(grb8_t in, led_type_t type);`

Implementation notes:
- HSV hue interpolation takes the shortest path around the hue wheel.
- Gray-edge cases (`s == 0`) are handled to avoid unstable hue transitions.
- `t` in interpolation APIs is `0..255`.

### `ld_board.h`

Defines:
- Topology constants (`LD_BOARD_WS2812B_NUM`, `LD_BOARD_WS2812B_MAX_PIXEL_NUM`, `LD_BOARD_PCA9955B_*`)
- `hw_config_t`
- `ch_info_t`
- Globals:
  - `extern const hw_config_t BOARD_HW_CONFIG;`
  - `extern ch_info_t ch_info;`

### `ld_frame.h`

Shared frame payload structs:
- `frame_data`
- `table_frame_t`

## Initialization Contract

Required startup order:

1. Call `calc_gamma_lut()` once in early startup.
2. Initialize `ch_info` with valid channel pixel counts.
3. Initialize modules that depend on `ch_info` (for example `LedController::init()`).
4. Run rendering pipeline (`lerp -> gamma -> brightness`) before hardware send.

If `ch_info` is empty or invalid, downstream modules may fail initialization or parse frame data incorrectly.

## Usage Example

```c
#include "ld_gamma_lut.h"
#include "ld_board.h"
#include "ld_led_ops.h"

void app_led_prepare(void) {
    calc_gamma_lut();

    for(int i = 0; i < LD_BOARD_WS2812B_NUM; ++i) {
        ch_info.rmt_strips[i] = LD_BOARD_WS2812B_MAX_PIXEL_NUM;
    }
    for(int i = 0; i < LD_BOARD_PCA9955B_CH_NUM; ++i) {
        ch_info.i2c_leds[i] = 1;
    }

    grb8_t c = grb8(255, 80, 20);
    c = grb_gamma_u8(c, LED_WS2812B);
    c = grb_set_brightness(c, LED_WS2812B);
}
```

## Build Integration

`components/ld_core/CMakeLists.txt` registers:
- Sources: `src/ld_board.c`, `src/ld_gamma_lut.c`
- Public include directory: `inc`
- Required dependency: `driver`

## Maintenance Notes

- Keep constants in `ld_board.h` synchronized with frame buffers and channel loops.
- Any gamma/brightness change should be validated on real hardware.
- `ld_led_ops.h` is header-inline heavy; changes affect all translation units that include it.
