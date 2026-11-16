/**
 * MIPI-CSI camera driver for ESP32-P4 using the esp_video V4L2 abstraction.
 *
 * The esp_video component handles the entire CSI + ISP pipeline internally:
 *   - CSI controller setup and DMA
 *   - ISP (RAW8 → RGB565) via CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER
 *   - Sensor detection and streaming (OV5647 via Kconfig)
 *
 * This driver uses the standard V4L2 ioctl interface for frame capture.
 * Implements the unified board_camera API (board_camera.h).
 */
#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI

#include "board_camera.h"

#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "board_camera_csi";

#define CSI_NUM_BUFS    3

static int video_fd = -1;
static uint8_t *frame_bufs[CSI_NUM_BUFS];
static size_t frame_buf_size = 0;
static uint16_t csi_frame_w = 0;
static uint16_t csi_frame_h = 0;

esp_err_t board_camera_init(void *i2c_bus, uint16_t width, uint16_t height)
{
    esp_err_t err;

    /* Step 1: Initialize esp_video with CSI config, reusing caller's I2C bus */
    esp_video_init_csi_config_t csi_config[] = {
        {
            .sccb_config = {
                .init_sccb = false,
                .i2c_handle = (i2c_master_bus_handle_t)i2c_bus,
                .freq = 400000,
            },
            .reset_pin = -1,
            .pwdn_pin = -1,
        },
    };
    esp_video_init_config_t cam_config = {
        .csi = csi_config,
    };
    err = esp_video_init(&cam_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Step 2: Open the MIPI-CSI V4L2 device */
    video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (video_fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return ESP_FAIL;
    }

    /* Step 3: Query format (defaults to RGB565 via ISP pipeline) */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_G_FMT, &fmt) < 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: errno=%d", errno);
        return ESP_FAIL;
    }

    csi_frame_w = fmt.fmt.pix.width;
    csi_frame_h = fmt.fmt.pix.height;
    /* sizeimage may be 0 from the driver; compute it ourselves */
    frame_buf_size = csi_frame_w * csi_frame_h * 2;  /* RGB565 = 2 BPP */
    ESP_LOGI(TAG, "Camera format: %dx%d, pixfmt=0x%08x (%zu bytes/frame)",
             csi_frame_w, csi_frame_h, fmt.fmt.pix.pixelformat, frame_buf_size);

    /* Step 4: Allocate frame buffers in PSRAM (cache-aligned for DMA) */
    const size_t cache_line_size = 128;  /* CONFIG_CACHE_L2_CACHE_LINE_128B */

    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        frame_bufs[i] = heap_caps_aligned_calloc(cache_line_size, 1,
                                                  frame_buf_size, MALLOC_CAP_SPIRAM);
        if (!frame_bufs[i]) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d (%zu bytes)", i, frame_buf_size);
            return ESP_ERR_NO_MEM;
        }
    }

    /* Step 5: Request buffers (V4L2 USERPTR mode — we supply our own buffers) */
    struct v4l2_requestbuffers req = {
        .count = CSI_NUM_BUFS,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        return ESP_FAIL;
    }

    /* Step 6: Queue all buffers */
    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
            .index = i,
            .m.userptr = (unsigned long)frame_bufs[i],
            .length = frame_buf_size,
        };
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%d] failed", i);
            return ESP_FAIL;
        }
    }

    /* Step 7: Start streaming.
     * Note: the ISP pipeline controller task and CSI DMA require enough
     * internal memory for descriptors.  If the system crashes after
     * STREAMON, check heap and stack sizes. */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed: errno=%d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "CSI camera streaming (%dx%d RGB565, %d buffers)",
             csi_frame_w, csi_frame_h, CSI_NUM_BUFS);
    return ESP_OK;
}

esp_err_t board_camera_fb_get(board_camera_frame_t *frame, uint32_t timeout_ms)
{
    (void)timeout_ms;  /* V4L2 DQBUF blocks until a frame is ready */

    struct v4l2_buffer buf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };

    if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
        ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d", errno);
        return ESP_ERR_TIMEOUT;
    }

    /* Use our own buffer array — V4L2 may not return userptr correctly */
    int idx = buf.index;
    frame->buf    = frame_bufs[idx];
    frame->len    = buf.bytesused ? buf.bytesused : frame_buf_size;
    frame->width  = csi_frame_w;
    frame->height = csi_frame_h;
    frame->_priv  = (void *)(uintptr_t)idx;
    return ESP_OK;
}

void board_camera_fb_return(board_camera_frame_t *frame)
{
    if (!frame) return;

    int idx = (int)(uintptr_t)frame->_priv;
    struct v4l2_buffer buf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
        .index = idx,
        .m.userptr = (unsigned long)frame_bufs[idx],
        .length = frame_buf_size,
    };
    ioctl(video_fd, VIDIOC_QBUF, &buf);
}

#endif /* BOARD_HAS_CAMERA && CAMERA_CSI */
