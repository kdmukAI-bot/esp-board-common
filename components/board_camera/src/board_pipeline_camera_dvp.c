/**
 * Camera pipeline DVP driver for ESP32-S3.
 *
 * Uses the esp32-camera component for DVP capture. The capture task
 * loops on esp_camera_fb_get(), byte-swaps RGB565 from big-endian
 * to little-endian, then feeds frames to the pipeline via callback.
 *
 * TODO: Full implementation — Phase 6 of the pipeline integration.
 */
#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_DVP

#include "board_pipeline_camera_dvp.h"
#include "esp_log.h"

static const char *TAG = "pipeline_cam_dvp";

static void *dvp_init(const void *platform_config)
{
    ESP_LOGE(TAG, "DVP pipeline driver not yet implemented");
    return NULL;
}

static esp_err_t dvp_start(void *handle, cam_pipeline_frame_cb_t frame_cb,
                           void *user_ctx, int core_id)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t dvp_stop(void *handle)
{
    return ESP_ERR_NOT_SUPPORTED;
}

static void dvp_deinit(void *handle) {}

static esp_err_t dvp_get_resolution(void *handle, uint32_t *width,
                                    uint32_t *height)
{
    return ESP_ERR_NOT_SUPPORTED;
}

const cam_pipeline_camera_driver_t board_pipeline_dvp_driver = {
    .init           = dvp_init,
    .start          = dvp_start,
    .stop           = dvp_stop,
    .deinit         = dvp_deinit,
    .get_resolution = dvp_get_resolution,
    .set_ae_target  = NULL,
    .set_focus      = NULL,
    .has_focus_motor = NULL,
};

#endif /* BOARD_HAS_CAMERA && CAMERA_DVP */
