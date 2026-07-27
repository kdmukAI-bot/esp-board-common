/**
 * Camera pipeline CSI driver for ESP32-P4.
 *
 * Wraps the esp_video V4L2 abstraction to implement the pipeline's
 * camera driver interface. Spawns a capture task that loops on
 * VIDIOC_DQBUF and feeds frames to the pipeline via callback.
 */
#pragma once

#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI

#include "cam_pipeline_camera_driver.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_bus_handle_t i2c_bus; /* For SCCB sensor communication (NULL to self-init) */
    int sccb_sda_pin;               /* SCCB SDA pin (used when i2c_bus is NULL) */
    int sccb_scl_pin;               /* SCCB SCL pin (used when i2c_bus is NULL) */
    int sccb_i2c_port;              /* SCCB I2C port (used when i2c_bus is NULL) */
    uint16_t ae_target;             /* Fixed exposure written to V4L2_CID_EXPOSURE at start
                                     * (raw sensor exposure register, NOT a normalized AE
                                     * setpoint; 0 = leave the sensor/ISP default). Range is
                                     * sensor-specific — see CONFIG_BOARD_CSI_AE_TARGET. */
    bool hmirror;                   /* Flip sensor readout left<->right (V4L2_CID_HFLIP) */
    bool vflip;                     /* Flip sensor readout top<->bottom (V4L2_CID_VFLIP) */
} board_pipeline_csi_config_t;

extern const cam_pipeline_camera_driver_t board_pipeline_csi_driver;

#endif /* BOARD_HAS_CAMERA && CAMERA_CSI */
