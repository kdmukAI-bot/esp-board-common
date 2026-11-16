/**
 * Unified camera abstraction — works for both DVP and MIPI-CSI cameras.
 *
 * The board_config.h BOARD_CAMERA_INTERFACE define selects which driver
 * implements these functions.  Application code uses this API exclusively
 * and never includes esp_camera.h or esp_cam_ctlr.h directly.
 */
#pragma once

#include "board.h"
#include "board_config.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if BOARD_HAS_CAMERA

/**
 * Camera frame buffer descriptor (interface-agnostic).
 * Returned by board_camera_fb_get(); must be returned via board_camera_fb_return().
 */
typedef struct {
    uint8_t *buf;       /**< Pointer to pixel data (RGB565) */
    size_t   len;       /**< Length of valid pixel data in bytes */
    uint16_t width;     /**< Frame width in pixels */
    uint16_t height;    /**< Frame height in pixels */
    void    *_priv;     /**< Internal — do not access */
} board_camera_frame_t;

/**
 * Initialise camera hardware.
 * Dispatches to DVP or CSI implementation based on BOARD_CAMERA_INTERFACE.
 *
 * @param i2c_bus I2C bus handle for camera control (SCCB).
 *                For DVP boards this is the main I2C bus.
 *                For CSI boards sharing the I2C bus with touch, pass the
 *                same bus handle returned by board_i2c_init().
 * @param width   Desired frame width in pixels (hint; actual may differ for CSI)
 * @param height  Desired frame height in pixels (hint; actual may differ for CSI)
 * @return ESP_OK on success
 */
esp_err_t board_camera_init(void *i2c_bus, uint16_t width, uint16_t height);

/**
 * Get the next camera frame.  Blocks until a frame is available or timeout.
 *
 * @param frame       Output frame descriptor (filled on success)
 * @param timeout_ms  Maximum wait in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t board_camera_fb_get(board_camera_frame_t *frame, uint32_t timeout_ms);

/**
 * Return a frame buffer obtained from board_camera_fb_get().
 * Must be called once for each successful fb_get.
 *
 * @param frame  Frame to return
 */
void board_camera_fb_return(board_camera_frame_t *frame);

#endif /* BOARD_HAS_CAMERA */

#ifdef __cplusplus
}
#endif
