/**
 * Camera pipeline DVP driver for ESP32-S3.
 *
 * Wraps the esp32-camera component to implement the pipeline's
 * camera driver interface. Spawns a capture task that loops on
 * esp_camera_fb_get and feeds frames to the pipeline via callback.
 */
#pragma once

#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_DVP

#include "cam_pipeline_camera_driver.h"

typedef struct {
    int pin_d0, pin_d1, pin_d2, pin_d3;
    int pin_d4, pin_d5, pin_d6, pin_d7;
    int pin_xclk, pin_pclk, pin_vsync, pin_href;
    int pin_pwdn, pin_reset;
    int xclk_freq_hz;
    int ledc_timer;
    int ledc_channel;
    int sccb_i2c_port;
    uint16_t frame_width;  /* Requested sensor output width */
    uint16_t frame_height; /* Requested sensor output height */
} board_pipeline_dvp_config_t;

extern const cam_pipeline_camera_driver_t board_pipeline_dvp_driver;

#endif /* BOARD_HAS_CAMERA && CAMERA_DVP */
