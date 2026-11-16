#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_DVP

#include "board_camera.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "board_camera";

/**
 * Map (width, height) to the DVP framesize_t enum.
 * The esp32-camera component uses its own enum for frame sizes.
 */
static framesize_t size_from_dims(uint16_t w, uint16_t h)
{
    if (w == 96  && h == 96)  return FRAMESIZE_96X96;
    if (w == 128 && h == 128) return FRAMESIZE_128X128;
    if (w == 240 && h == 240) return FRAMESIZE_240X240;
    if (w == 320 && h == 320) return FRAMESIZE_320X320;
    if (w == 320 && h == 240) return FRAMESIZE_QVGA;
    if (w == 640 && h == 480) return FRAMESIZE_VGA;
    ESP_LOGW(TAG, "No exact framesize for %dx%d, defaulting to QVGA", w, h);
    return FRAMESIZE_QVGA;
}

esp_err_t board_camera_init(void *i2c_bus, uint16_t width, uint16_t height)
{
    (void)i2c_bus;  /* DVP uses BOARD_I2C_PORT directly */
    /* Force-stop the LEDC channel before esp_camera_init configures it.
     * LEDC registers in the RTC domain can retain stale state across
     * brief power cycles, causing the XCLK to not start reliably. */
    ledc_stop(LEDC_LOW_SPEED_MODE, BOARD_CAM_LEDC_CHANNEL, 0);

    camera_config_t config;
    config.ledc_channel = BOARD_CAM_LEDC_CHANNEL;
    config.ledc_timer = BOARD_CAM_LEDC_TIMER;
    config.pin_d0 = BOARD_PIN_CAM_Y2;
    config.pin_d1 = BOARD_PIN_CAM_Y3;
    config.pin_d2 = BOARD_PIN_CAM_Y4;
    config.pin_d3 = BOARD_PIN_CAM_Y5;
    config.pin_d4 = BOARD_PIN_CAM_Y6;
    config.pin_d5 = BOARD_PIN_CAM_Y7;
    config.pin_d6 = BOARD_PIN_CAM_Y8;
    config.pin_d7 = BOARD_PIN_CAM_Y9;
    config.pin_xclk = BOARD_PIN_CAM_XCLK;
    config.pin_pclk = BOARD_PIN_CAM_PCLK;
    config.pin_vsync = BOARD_PIN_CAM_VSYNC;
    config.pin_href = BOARD_PIN_CAM_HREF;
    config.pin_sccb_sda = -1;
    config.pin_sccb_scl = -1;
    config.sccb_i2c_port = BOARD_I2C_PORT;
    config.pin_pwdn = BOARD_PIN_CAM_PWDN;
    config.pin_reset = BOARD_PIN_CAM_RESET;
    config.xclk_freq_hz = BOARD_CAM_XCLK_FREQ;
    config.frame_size = size_from_dims(width, height);
    config.pixel_format = PIXFORMAT_RGB565;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);

    ESP_LOGI(TAG, "DVP camera initialized (%dx%d)", width, height);
    return ESP_OK;
}

esp_err_t board_camera_fb_get(board_camera_frame_t *frame, uint32_t timeout_ms)
{
    (void)timeout_ms;  /* DVP esp_camera_fb_get() blocks internally */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return ESP_ERR_TIMEOUT;

    frame->buf    = fb->buf;
    frame->len    = fb->len;
    frame->width  = fb->width;
    frame->height = fb->height;
    frame->_priv  = fb;
    return ESP_OK;
}

void board_camera_fb_return(board_camera_frame_t *frame)
{
    if (frame && frame->_priv) {
        esp_camera_fb_return((camera_fb_t *)frame->_priv);
        frame->_priv = NULL;
    }
}

#endif /* BOARD_HAS_CAMERA && CAMERA_DVP */
