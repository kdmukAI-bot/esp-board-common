/**
 * Board-level camera pipeline configuration.
 *
 * Builds a cam_pipeline_config_t pre-populated from board_config.h defines,
 * selecting the correct camera driver (DVP or CSI) and LVGL display driver.
 */
#pragma once

#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA

#include "esp_cam_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build a cam_pipeline_config_t pre-populated from board_config.h.
 *
 * @param display_parent  LVGL parent object (e.g., lv_screen_active())
 * @param i2c_bus         I2C bus handle for camera SCCB (CSI boards).
 *                        Pass board_i2c_get_handle() when the camera shares
 *                        the main I2C bus, or a separately initialized bus
 *                        for boards where the camera uses a different port.
 *                        Ignored for DVP boards.
 * @return Filled config struct ready for cam_pipeline_create()
 */
cam_pipeline_config_t board_pipeline_default_config(void *display_parent,
                                                    void *i2c_bus);

/**
 * How the auto-exposure loop weights the scene when it measures brightness.
 *
 * Only boards whose camera runs an ISP auto-exposure loop (an IPA tuning file;
 * see BOARD_CAMERA_IPA_CONFIG_NAME) can honour this. Elsewhere the setter
 * reports ESP_ERR_NOT_SUPPORTED and exposure behaviour is unchanged.
 */
typedef enum {
    /** Whatever the board's tuning file specifies. */
    BOARD_CAM_AE_METERING_DEFAULT = 0,
    /** Flat across the frame — every region counts equally. */
    BOARD_CAM_AE_METERING_AVERAGE,
    /** Weighted toward the middle of the frame. */
    BOARD_CAM_AE_METERING_CENTER,
} board_cam_ae_metering_t;

/**
 * Select the auto-exposure metering profile.
 *
 * The setting is board-global (one exposure loop serves every camera session),
 * so callers set it for the session they are about to open, before
 * cam_pipeline_create(). It takes effect immediately once the loop is running,
 * and is remembered if it is not yet.
 */
esp_err_t board_pipeline_set_ae_metering(board_cam_ae_metering_t mode);

/**
 * Override the auto-exposure setpoint — the image brightness the loop aims for,
 * in the ISP's 0-255 luma scale. Pass 0 to go back to the board tuning file's own
 * value. The quiescent band around it scales with the setpoint, keeping the
 * tuning file's proportions.
 *
 * Takes effect on the next metered frame, so it can be swept live while the
 * camera runs. Same availability rule as board_pipeline_set_ae_metering().
 */
esp_err_t board_pipeline_set_ae_luma_target(uint8_t target);

/** Setpoint currently in force, or 0 if no auto-exposure loop is running. */
uint8_t board_pipeline_get_ae_luma_target(void);

/**
 * Set the ISP tone curve: a display gamma plus a black point, as one hardware
 * lookup table.
 *
 * @param gamma_x10    Display gamma x10 (22 = 2.2). 0 leaves the ISP linear,
 *                     which looks dim and flat on a normal panel.
 * @param black_level  Input level mapped to true black, 0-64. Compensates the
 *                     sensor's own black offset, which nothing else subtracts,
 *                     so shadows reach zero instead of floating grey.
 *
 * Applied per camera session as a single ioctl; the curve then costs no
 * per-frame CPU. Boards default it via BOARD_CAMERA_TONE_GAMMA_X10 /
 * BOARD_CAMERA_TONE_BLACK_LEVEL.
 */
esp_err_t board_pipeline_set_tone(uint8_t gamma_x10, uint8_t black_level);

/**
 * Trim the ISP colour block: saturation and contrast, 128 = neutral for both,
 * 0-255. Both are hardware, applied after the gamma curve, so they cost one ioctl
 * per camera session and nothing per frame. Boards default them via
 * BOARD_CAMERA_COLOR_SATURATION / BOARD_CAMERA_COLOR_CONTRAST.
 */
esp_err_t board_pipeline_set_color(uint8_t saturation, uint8_t contrast);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_HAS_CAMERA */
