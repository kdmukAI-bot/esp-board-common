# esp-board-common

Hardware abstraction layer (HAL) for Waveshare ESP32 display boards. Provides a single `board_init()` call that initializes the display, touch, PMIC, and peripherals for any supported board, selected at compile time.

Built as an ESP-IDF component. Apps reference it via `EXTRA_COMPONENT_DIRS` and select a board with `-DBOARD=<name>`.

## Supported boards

| Board | SoC | Display | Touch | Camera |
|-------|-----|---------|-------|--------|
| `waveshare_s3_lcd35b` | ESP32-S3 | AXS15231B 320x480 QSPI | AXS15231B | OV5640 DVP |
| `waveshare_s3_lcd35` | ESP32-S3 | ST7796 320x480 SPI | FT6336 | OV5640 DVP |
| `waveshare_s3_lcd2` | ESP32-S3 | ST7789 240x320 SPI | CST816D | -- |
| `waveshare_p4_lcd35` | ESP32-P4 | ST7796 320x480 SPI | FT6336 | OV5647 CSI |
| `waveshare_p4_lcd43` | ESP32-P4 | ST7701 480x800 MIPI-DSI | GT911 | OV5647 CSI |

## Project structure

```
esp-board-common/
├── src/                    # Core component source (drivers, init, LVGL integration, overlays)
├── components/             # Optional components
│   ├── board_camera/       # Camera drivers, pipeline glue, QR scan coordinator
│   ├── ov02c10/            # OV02C10 sensor add-on for esp_cam_sensor (P4)
│   ├── board_log_flash/    # Tee esp_log output to a flash partition
│   └── esp-camera-pipeline/  # Submodule: frame pipeline, QR and entropy consumers
├── boards/                 # Per-board config (board_config.h, sdkconfig.defaults)
│   ├── waveshare_s3_lcd35b/
│   ├── waveshare_s3_lcd35/
│   ├── waveshare_s3_lcd2/
│   ├── waveshare_p4_lcd35/
│   └── waveshare_p4_lcd43/
├── apps/                   # Standalone demo applications
│   ├── qr_decoder/
│   └── touch_test/
├── CMakeLists.txt          # ESP-IDF component registration
├── idf_component.yml       # Component registry dependencies
└── docs/
```

## Usage

### As an ESP-IDF component

In your app's top-level `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/esp-board-common")
```

In your app's `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES "esp-board-common"
)
```

Build with a board selection:

```
idf.py -DBOARD=waveshare_s3_lcd35b build
```

### Camera support

The camera lives in its own component, `board_camera`, so an app without a camera never lists the camera drivers as dependencies, fetches them, or builds them. Such an app needs only the esp-board-common directory on its search path, as above ([apps/touch_test](apps/touch_test/) is an example).

An app that uses the camera adds esp-board-common's `components/` and the camera pipeline's nested `components/` to the search path, and requires `board_camera`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "/path/to/esp-board-common"
    "/path/to/esp-board-common/components"
    "/path/to/esp-board-common/components/esp-camera-pipeline/components"
)
```

```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES "esp-board-common" "board_camera"
)
```

ESP-IDF builds every component it finds on the search path, so an app without a camera that wants another optional component, such as `board_log_flash`, should list that component's directory alone rather than all of `components/`. See [apps/qr_decoder](apps/qr_decoder/) for a complete camera app.

### Minimal app

```c
#include "board.h"
#include "board_backlight.h"

void app_main(void)
{
    lv_display_t *disp;
    lv_indev_t *touch;

    board_app_config_t cfg = { .landscape = false };
    board_init(&cfg, &disp, &touch);
    board_backlight_set(100);

    // LVGL is ready: create widgets, start tasks, etc.

    board_run();  // never returns
}
```

## Display interfaces

| Interface | Boards | Driver |
|-----------|--------|--------|
| SPI | S3 LCD 3.5, S3 LCD 2, P4 LCD 3.5 | Standard SPI with DMA partial updates |
| QSPI | S3 LCD 3.5B | Direct mode with RASET workaround |
| MIPI-DSI | P4 LCD 4.3 | DPI video mode with `lvgl_port_add_disp_dsi()`, triple-buffered |

## Camera interfaces

| Interface | Boards | Driver |
|-----------|--------|--------|
| DVP | S3 boards | `espressif/esp32-camera` (`esp_camera_fb_get`) |
| MIPI-CSI | P4 boards | `esp_video` V4L2 layer with ISP pipeline (RAW8 → RGB565) |

Both interfaces use the same unified API (`board_camera_init`, `board_camera_fb_get`, `board_camera_fb_return`), provided by the `board_camera` component (see [Camera support](#camera-support)). Application code never includes interface-specific headers.

On ESP32-P4 boards, the PPA (Pixel Processing Accelerator) hardware engine provides zero-CPU-cost downscaling from the sensor resolution (800x1280) to the display (480x800). The camera viewfinder app demonstrates this with fullscreen, hardware-accelerated live video.

## Text overlays

Reusable text overlay modules for rendering on-screen text alongside a live camera feed. Two backends handle different display interfaces:

| Module | Interface | Approach |
|--------|-----------|----------|
| `overlay_text` | SPI (dummy-draw) | Direct LVGL font glyph rendering into gap-area buffers, with DMA panel writes. Supports portrait and landscape (CPU-rotated + black-keyed compositing). |
| `overlay_rotated` | MIPI-DSI (landscape) | Hidden LVGL canvas rendering, bounding-box crop, CPU 90° CCW rotation, displayed as `lv_image`. Supports RGB565 (opaque) and ARGB8888 (transparent). |

Shared RGB565 utilities (`rgb565_pack`, `alpha_blend_rgb565`, `copy_swap_u16`) live in `overlay_util.h` (header-only).

The SPI backend renders directly because LVGL is halted in dummy-draw mode. The DSI backend uses LVGL canvas/image widgets because `transform_rotation` is incompatible with `direct_mode`. See `docs/knowledge/text-overlay-architecture.md` for the full design rationale.

## API

### `board_init()`

Initializes all hardware in the correct order (I2C, IO expander, display, PMIC, touch, backlight, LVGL) based on `board_config.h`. Returns LVGL display and touch handles.

### `board_set_render_interval_ms(interval_ms)`

Dynamically controls LVGL's render rate. Use a small value (e.g. 10) for real-time camera or animation, or 0 to revert to the default idle behavior. Must hold the LVGL port lock when calling.

### `board_camera_init(i2c_bus, width, height)`

Initializes the camera hardware. Dispatches to DVP or CSI implementation based on `BOARD_CAMERA_INTERFACE`. The `i2c_bus` handle is obtained from `board_i2c_get_handle()` after `board_init()`.

### `board_camera_fb_get(frame, timeout_ms)` / `board_camera_fb_return(frame)`

Synchronous frame capture. `fb_get` blocks until a frame is available; `fb_return` releases the buffer back to the driver. Works identically on DVP and CSI.

## Building apps

See [apps/qr_decoder/](apps/qr_decoder/) for a complete example with Docker build, camera pipeline, text overlays, and per-board dist output.
