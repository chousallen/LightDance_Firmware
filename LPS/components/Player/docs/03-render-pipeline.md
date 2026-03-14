# 03 - Render Pipeline

Render logic is centered in `FrameBuffer` (`src/framebuffer.cpp`).

## Responsibilities

- Hold two timeline frames: `current` and `next`
- Advance frame pointers as time grows
- Interpolate outputs per tick
- Apply gamma and brightness correction
- Expose final `frame_data` buffer

## Compute Paths

`compute(time_ms)` has two modes.

### Test Mode

- `SOLID`: fill one color across all outputs
- `BREATH`: generate hue from time and convert HSV to GRB
- then apply gamma and brightness

### Normal Playback

1. `handle_frames(time_ms)` advances keyframes if needed
2. Compute interpolation factor `p`:
   - fade: `p = calc_lerp_p(...)`
   - step: `p = 0`
3. `lerp(p)` with HSV interpolation (`grb_lerp_hsv_u8`)
4. gamma correction
5. brightness correction

## Data Source

- If `LD_CFG_ENABLE_PT` is enabled: `read_frame(...)`
- Otherwise: `test_read_frame(...)`

## Output Contract

`get_buffer()` returns internal `frame_data*`.
Caller (`Player`) must consume it before next compute cycle.
