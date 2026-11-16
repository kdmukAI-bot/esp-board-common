/**
 * Camera pipeline CSI driver for ESP32-P4.
 *
 * Uses the esp_video V4L2 abstraction for MIPI-CSI capture. The ISP
 * pipeline (RAW8 → RGB565) runs internally via
 * CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER.
 *
 * init()  — inits esp_video ONCE per boot, opens V4L2 device, allocates
 *           buffers, queues them
 * start() — begins streaming and spawns a capture task
 * stop()  — stops streaming and joins the capture task
 * deinit()— frees buffers and closes the device; leaves esp_video registered
 *           (no public esp_video_deinit exists — see s_esp_video_inited)
 */
#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI

#include "board_pipeline_camera_csi.h"

/* ── Adaptive AE / AWB via the esp_ipa ISP pipeline ─────────────────────────
 *
 * A board opts in by defining BOARD_CAMERA_IPA_CONFIG_NAME as the sensor key in
 * its IPA tuning JSON (see the ov02c10 component's cfg/). With it defined, this
 * file creates the ISP pipeline itself instead of letting esp_video do it. Two
 * reasons:
 *
 *  1. esp_video_init() looks a tuning file up by the sensor's OWN name and, on a
 *     hit, hands esp_video_isp_pipeline_init() the flash-resident `const` config
 *     straight from the generated lookup table. Nothing can retune it afterwards.
 *     Our tuning file is therefore keyed to a name the sensor does not report, so
 *     that lookup misses and we get to supply a RAM copy we still own.
 *  2. Owning the copy is what makes per-session AE metering possible: esp_ipa's
 *     AGC re-reads luma_weight_table (and the luma thresholds) from the config
 *     pointer on EVERY frame, so writing the RAM copy between camera sessions
 *     changes how the next session meters the scene.
 *
 * The one thing it caches is sum(luma_weight_table), computed once when the
 * pipeline is created. Every profile table below must therefore sum to the same
 * value as the table in the tuning file — enforced at runtime in
 * csi_apply_ae_metering(). See docs/knowledge/esp32-p4-ipa-adaptive-ae-awb-ov02c10.md.
 *
 * A board with no tuning file (the OV5647 targets) compiles all of this out and
 * keeps esp_video's own behaviour untouched. */
#if defined(BOARD_CAMERA_IPA_CONFIG_NAME) && CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER
#define CSI_HAS_IPA 1
#else
#define CSI_HAS_IPA 0
#endif

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/ioctl.h>
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_isp_ioctl.h"
#include "driver/isp_gamma.h"
#if CSI_HAS_IPA
#include "esp_ipa.h"
/* esp_video PRIVATE header — see the board_common CMakeLists note that adds it. */
#include "esp_video_pipeline_isp.h"
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "pipeline_cam_csi";

/* esp_video registers board-global singletons (ISP/CSI video devices, the ISP
 * pipeline-controller task, the MIPI LDO channel) and exposes NO public
 * esp_video_deinit() — the ISP controller task handle isn't even retained, so
 * it cannot be stopped. Registering twice fails ("video name=ISP id=20 has
 * been registered"), which is why an in-boot pipeline stop/start used to force
 * a machine.reset(). Init it exactly once per boot and leave it registered;
 * subsequent scan cycles just re-open the V4L2 device + re-REQBUFS/STREAMON. */
static bool s_esp_video_inited = false;

#define CSI_NUM_BUFS      3
#define CSI_TASK_STACK     (16 * 1024)
/* Flattened to 1 (== lvgl + MicroPython VM tasks) for FIFO-fair LVGL-lock access;
 * see BOARD_LVGL_TASK_PRIORITY note. Dropped frames are acceptable here. */
#define CSI_TASK_PRIORITY  1

/* AE target: 2-235 range per esp_video ISP pipeline.
 * Higher = brighter exposure. 128 = neutral. Kern defaults to 80.
 * Mixed use case: paper QR codes under ambient light + display QR codes.
 * Slightly below neutral to avoid blowing out backlit displays. */
#define CSI_AE_TARGET_DEFAULT  100

typedef struct {
    int video_fd;
    uint8_t *frame_bufs[CSI_NUM_BUFS];
    size_t frame_buf_size;
    uint16_t frame_w;
    uint16_t frame_h;
    uint16_t ae_target;             /* fixed V4L2_CID_EXPOSURE at start; 0 = sensor default */
    bool hmirror;
    bool vflip;
    cam_pipeline_frame_cb_t frame_cb;
    void *user_ctx;
    TaskHandle_t task_handle;
    SemaphoreHandle_t task_done_sem;  /* task gives it on exit; stop() joins on it */
    volatile bool running;
} csi_driver_ctx_t;

/* Forward declaration */
static esp_err_t csi_set_ae_target(void *handle, uint32_t level);

/* ── ISP tone curve: black point + display gamma ────────────────────────────
 *
 * The ISP's GAMMA block is BYPASSED unless something writes a curve to it —
 * esp_video only enables it on a V4L2_CID_USER_ESP_ISP_GAMMA write. With no IPA
 * enhancement block driving it, the pipeline hands the display LINEAR light,
 * which reads as dim and flat: a display expects roughly sRGB-encoded input, so
 * every midtone lands far darker than the eye expects.
 *
 * The same 16-point curve also fixes the black point. The sensor's own black
 * offset is never subtracted (the ESP32-P4 ISP has a black-level block, but
 * ESP-IDF 5.5.1 ships no driver for it and esp_video exposes no control), so the
 * darkest pixels float around 9/255 instead of reaching zero and shadows read as
 * washed grey. Folding the subtraction into the curve costs nothing extra and
 * has to happen here anyway: applying gamma to an un-subtracted pedestal would
 * LIFT it further (a 0.45 encode maps 9/255 to roughly 60/255), making the very
 * problem worse.
 *
 *      y = 255 * clamp((x - black) / (255 - black), 0, 1) ^ (1 / gamma)
 *
 * Cost is one ioctl per camera session. The curve itself is a hardware LUT, so
 * there is no per-frame CPU at all. */
#ifndef BOARD_CAMERA_TONE_GAMMA_X10
#define BOARD_CAMERA_TONE_GAMMA_X10 0   /* 0 = leave the ISP linear (no curve) */
#endif
#ifndef BOARD_CAMERA_TONE_BLACK_LEVEL
#define BOARD_CAMERA_TONE_BLACK_LEVEL 0
#endif

static uint8_t s_tone_gamma_x10 = BOARD_CAMERA_TONE_GAMMA_X10;
static uint8_t s_tone_black     = BOARD_CAMERA_TONE_BLACK_LEVEL;

/* esp_isp_gamma_fill_curve_points() takes a context-free function pointer, hence
 * the file-scope parameters above. */
static uint32_t csi_tone_curve(uint32_t x)
{
    float black = (float)s_tone_black;
    float span = 255.0f - black;
    if (span < 1.0f) {
        span = 1.0f;
    }

    float v = ((float)x - black) / span;
    if (v <= 0.0f) {
        return 0;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }

    float y = powf(v, 10.0f / (float)s_tone_gamma_x10) * 255.0f + 0.5f;
    return (uint32_t)(y > 255.0f ? 255.0f : y);
}

static void csi_apply_tone_curve(void)
{
    int fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        ESP_LOGW(TAG, "Tone curve: cannot open %s (errno=%d)",
                 ESP_VIDEO_ISP1_DEVICE_NAME, errno);
        return;
    }

    esp_video_isp_gamma_t gamma = { .enable = s_tone_gamma_x10 > 0 };
    if (gamma.enable) {
        /* The hardware constrains the point spacing to powers of two; let the IDF
         * helper place them rather than hand-rolling the x axis. */
        isp_gamma_curve_points_t pts;
        esp_err_t err = esp_isp_gamma_fill_curve_points(csi_tone_curve, &pts);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Tone curve: fill failed: %s", esp_err_to_name(err));
            close(fd);
            return;
        }
        for (int i = 0; i < ISP_GAMMA_CURVE_POINTS_NUM; i++) {
            gamma.points[i].x = pts.pt[i].x;
            gamma.points[i].y = pts.pt[i].y;
        }
    }

    struct v4l2_ext_control ctrl = {
        .id = V4L2_CID_USER_ESP_ISP_GAMMA,
        .size = sizeof(gamma),
        .p_u8 = (uint8_t *)&gamma,
    };
    struct v4l2_ext_controls ctrls = {
        .ctrl_class = V4L2_CTRL_CLASS_USER, .count = 1, .controls = &ctrl };
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        ESP_LOGW(TAG, "Tone curve: set failed (errno=%d)", errno);
    } else if (gamma.enable) {
        ESP_LOGI(TAG, "Tone curve: gamma %u.%u, black point %u",
                 s_tone_gamma_x10 / 10, s_tone_gamma_x10 % 10, s_tone_black);
    } else {
        ESP_LOGI(TAG, "Tone curve: disabled (linear)");
    }
    close(fd);
}

/* Saturation and contrast live in the ISP's colour block — hardware, applied after
 * the gamma LUT, 128 = neutral for both. Worth having separately from the IPA's own
 * saturation table: that one is indexed by colour temperature and only moves when
 * the tuning file says so, whereas this is a flat board-level trim. */
#ifndef BOARD_CAMERA_COLOR_SATURATION
#define BOARD_CAMERA_COLOR_SATURATION 128
#endif
#ifndef BOARD_CAMERA_COLOR_CONTRAST
#define BOARD_CAMERA_COLOR_CONTRAST 128
#endif

static uint8_t s_color_saturation = BOARD_CAMERA_COLOR_SATURATION;
static uint8_t s_color_contrast   = BOARD_CAMERA_COLOR_CONTRAST;

static void csi_apply_color(void)
{
    if (s_color_saturation == 128 && s_color_contrast == 128) {
        return;   /* neutral: leave the ISP defaults untouched */
    }

    int fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        ESP_LOGW(TAG, "Colour: cannot open %s (errno=%d)",
                 ESP_VIDEO_ISP1_DEVICE_NAME, errno);
        return;
    }

    struct v4l2_ext_control ctrl[2] = {
        { .id = V4L2_CID_SATURATION, .value = s_color_saturation },
        { .id = V4L2_CID_CONTRAST,   .value = s_color_contrast },
    };
    struct v4l2_ext_controls ctrls = {
        .ctrl_class = V4L2_CTRL_CLASS_USER, .count = 2, .controls = ctrl };
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) < 0) {
        ESP_LOGW(TAG, "Colour: set failed (errno=%d)", errno);
    } else {
        ESP_LOGI(TAG, "Colour: saturation %u, contrast %u",
                 s_color_saturation, s_color_contrast);
    }
    close(fd);
}

esp_err_t board_pipeline_csi_set_color(uint8_t saturation, uint8_t contrast)
{
    s_color_saturation = saturation;
    s_color_contrast = contrast;
    csi_apply_color();
    return ESP_OK;
}

esp_err_t board_pipeline_csi_set_tone(uint8_t gamma_x10, uint8_t black_level)
{
    if (gamma_x10 && (gamma_x10 < 5 || gamma_x10 > 40)) {
        return ESP_ERR_INVALID_ARG;   /* 0.5 .. 4.0 */
    }
    if (black_level > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    s_tone_gamma_x10 = gamma_x10;
    s_tone_black = black_level;
    csi_apply_tone_curve();
    return ESP_OK;
}

/* ── AE metering profiles ── */
#if CSI_HAS_IPA

/* Weights over the ISP's 5x5 auto-exposure region grid, row-major over the full
 * sensor frame. BOTH tables must sum to the same total as the table in the tuning
 * JSON (csi_apply_ae_metering enforces it) — esp_ipa's AGC caches that sum once
 * and divides the weighted luma by it every frame, so a mismatched table would
 * scale the measured brightness and push the exposure loop off target. */

/* Whole-scene average. Flat, so a bright or dark corner pulls exactly as hard as
 * the middle — wanted for image entropy, where the whole frame is the subject. */
static const uint8_t k_ae_weight_average[ISP_AE_REGIONS] = {
    3, 3, 3, 3, 3,
    3, 3, 3, 3, 3,
    3, 3, 3, 3, 3,
    3, 3, 3, 3, 3,
    3, 3, 3, 3, 3,
};

/* Centre-weighted: the middle 3x3 carries ~73% of the total. For QR scanning the
 * code is held in the middle of the frame and the surround is whatever happens to
 * be behind it — a bright desk lamp or a dark background at the edge would
 * otherwise drag the exposure away from the one region that has to decode. The
 * surround still contributes, so a code on a dim background does not blow out. */
static const uint8_t k_ae_weight_center[ISP_AE_REGIONS] = {
    1,  1,  2,  1,  1,
    1,  4,  6,  4,  1,
    2,  6, 15,  6,  2,
    1,  4,  6,  4,  1,
    1,  1,  2,  1,  1,
};

/* RAM copies handed to esp_video_isp_pipeline_init(). s_agc_cfg is the one the
 * AGC re-reads per frame, so retuning AE means writing it in place. */
static esp_ipa_config_t s_ipa_cfg;
static esp_ipa_agc_config_t s_agc_cfg;
static uint8_t s_ae_weight_tuned[ISP_AE_REGIONS];  /* the tuning file's own table */
static uint16_t s_ae_weight_sum;                   /* the sum esp_ipa cached */
static bool s_ipa_started = false;
static board_cam_ae_metering_t s_ae_metering = BOARD_CAM_AE_METERING_DEFAULT;

/* The tuning file's own setpoint + band, kept so an override can be undone and so
 * the band can be rescaled in the file's proportions. */
static uint8_t s_luma_target_tuned, s_luma_low_tuned, s_luma_high_tuned;
static uint8_t s_luma_target_override;             /* 0 = use the tuning file's */

static void csi_apply_ae_luma_target(void)
{
    if (!s_luma_target_override) {
        s_agc_cfg.luma_target = s_luma_target_tuned;
        s_agc_cfg.luma_low    = s_luma_low_tuned;
        s_agc_cfg.luma_high   = s_luma_high_tuned;
        return;
    }

    /* Scale the quiescent band with the setpoint so its width stays proportional —
     * a band sized for a dark target would thrash at a bright one. esp_ipa requires
     * luma_low < target < luma_high, so clamp to keep that true at the extremes. */
    uint32_t t = s_luma_target_override;
    uint32_t lo = (uint32_t)s_luma_low_tuned * t / s_luma_target_tuned;
    uint32_t hi = (uint32_t)s_luma_high_tuned * t / s_luma_target_tuned;
    if (lo < 3) lo = 3;
    if (lo >= t) lo = t - 1;
    if (hi > 252) hi = 252;
    if (hi <= t) hi = t + 1;

    s_agc_cfg.luma_target = (uint8_t)t;
    s_agc_cfg.luma_low    = (uint8_t)lo;
    s_agc_cfg.luma_high   = (uint8_t)hi;
}

esp_err_t board_pipeline_csi_set_ae_luma_target(uint8_t target)
{
    /* esp_ipa's usable setpoint range; 0 is our "revert to the tuning file" value. */
    if (target && (target < 4 || target > 251)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_luma_target_override = target;
    if (!s_ipa_started) {
        return ESP_OK;  /* applied when the pipeline starts */
    }
    csi_apply_ae_luma_target();
    ESP_LOGI(TAG, "AE setpoint: luma %u (band %u-%u)",
             s_agc_cfg.luma_target, s_agc_cfg.luma_low, s_agc_cfg.luma_high);
    return ESP_OK;
}

uint8_t board_pipeline_csi_get_ae_luma_target(void)
{
    return s_ipa_started ? s_agc_cfg.luma_target : 0;
}

static esp_err_t csi_apply_ae_metering(void)
{
    const uint8_t *table;
    const char *name;

    switch (s_ae_metering) {
    case BOARD_CAM_AE_METERING_AVERAGE:
        table = k_ae_weight_average; name = "average"; break;
    case BOARD_CAM_AE_METERING_CENTER:
        table = k_ae_weight_center;  name = "centre-weighted"; break;
    default:
        table = s_ae_weight_tuned;   name = "tuning-file default"; break;
    }

    uint16_t sum = 0;
    for (int i = 0; i < ISP_AE_REGIONS; i++) {
        sum += table[i];
    }
    if (sum != s_ae_weight_sum) {
        ESP_LOGE(TAG, "AE metering table sum %u != tuning file's %u; ignoring "
                      "(a mismatched sum mis-scales the exposure loop)",
                 sum, s_ae_weight_sum);
        return ESP_ERR_INVALID_ARG;
    }

    /* The AGC only reads this while frames are being processed, and metering is
     * selected between camera sessions (nothing is streaming), so an in-place
     * copy needs no lock. Worst case mid-stream is one frame metered on a mixed
     * table, which the loop corrects on the next. */
    if (memcmp(s_agc_cfg.luma_weight_table, table, ISP_AE_REGIONS) != 0) {
        memcpy(s_agc_cfg.luma_weight_table, table, ISP_AE_REGIONS);
        ESP_LOGI(TAG, "AE metering: %s", name);
    }
    return ESP_OK;
}

/* Create the ISP/IPA pipeline with our own configuration. Called exactly once
 * per boot, right after esp_video_init() — the same point esp_video would have
 * created it, had our tuning file been keyed to the sensor's own name. */
static void csi_ipa_start(void)
{
    const esp_ipa_config_t *base =
        esp_ipa_pipeline_get_config(BOARD_CAMERA_IPA_CONFIG_NAME);
    if (!base || !base->agc) {
        ESP_LOGE(TAG, "No IPA tuning for '%s'%s — camera runs without adaptive "
                      "exposure or white balance",
                 BOARD_CAMERA_IPA_CONFIG_NAME, base ? " (no agc block)" : "");
        return;
    }

    s_ipa_cfg = *base;
    s_agc_cfg = *base->agc;
    s_ipa_cfg.agc = &s_agc_cfg;

    memcpy(s_ae_weight_tuned, s_agc_cfg.luma_weight_table, ISP_AE_REGIONS);
    s_ae_weight_sum = 0;
    for (int i = 0; i < ISP_AE_REGIONS; i++) {
        s_ae_weight_sum += s_ae_weight_tuned[i];
    }
    s_luma_target_tuned = s_agc_cfg.luma_target;
    s_luma_low_tuned    = s_agc_cfg.luma_low;
    s_luma_high_tuned   = s_agc_cfg.luma_high;

    esp_video_isp_config_t isp_config = {
        .cam_dev = ESP_VIDEO_MIPI_CSI_DEVICE_NAME,
        .isp_dev = ESP_VIDEO_ISP1_DEVICE_NAME,
        .ipa_config = &s_ipa_cfg,
    };
    esp_err_t err = esp_video_isp_pipeline_init(&isp_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISP pipeline init failed: %s", esp_err_to_name(err));
        return;
    }

    s_ipa_started = true;
    ESP_LOGI(TAG, "ISP pipeline running IPA '%s' (adaptive AE + AWB, "
                  "luma target %u, metering weight sum %u)",
             BOARD_CAMERA_IPA_CONFIG_NAME, s_agc_cfg.luma_target, s_ae_weight_sum);

    /* Honour anything selected before the first camera session. */
    csi_apply_ae_metering();
    csi_apply_ae_luma_target();
}

esp_err_t board_pipeline_csi_set_ae_metering(board_cam_ae_metering_t mode)
{
    s_ae_metering = mode;
    /* Before the pipeline exists the selection is just remembered; csi_ipa_start
     * applies it. */
    return s_ipa_started ? csi_apply_ae_metering() : ESP_OK;
}

#else /* !CSI_HAS_IPA */

esp_err_t board_pipeline_csi_set_ae_metering(board_cam_ae_metering_t mode)
{
    (void)mode;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t board_pipeline_csi_set_ae_luma_target(uint8_t target)
{
    (void)target;
    return ESP_ERR_NOT_SUPPORTED;
}

uint8_t board_pipeline_csi_get_ae_luma_target(void)
{
    return 0;
}

#endif /* CSI_HAS_IPA */

/* ── Capture task ── */

static void csi_capture_task(void *param)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)param;

    ESP_LOGI(TAG, "Capture task started (%dx%d)", ctx->frame_w, ctx->frame_h);

    while (ctx->running) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_USERPTR,
        };

        if (ioctl(ctx->video_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (ctx->running) {
                ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d", errno);
            }
            break;
        }

        int idx = buf.index;
        ctx->frame_cb(ctx->frame_bufs[idx], ctx->frame_w, ctx->frame_h,
                       ctx->user_ctx);

        /* Return buffer to V4L2 queue */
        struct v4l2_buffer qbuf = {
            .type    = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory  = V4L2_MEMORY_USERPTR,
            .index   = idx,
            .m.userptr = (unsigned long)ctx->frame_bufs[idx],
            .length  = ctx->frame_buf_size,
        };
        ioctl(ctx->video_fd, VIDIOC_QBUF, &qbuf);
    }

    ESP_LOGI(TAG, "Capture task exiting");
    /* Signal csi_stop that we've left the loop and are self-deleting. csi_stop
     * joins on this BEFORE calling STREAMOFF, so we always exit here off a real
     * frame (DQBUF returns while streaming is still on) instead of being left
     * blocked in DQBUF -- see the note in csi_stop. */
    if (ctx->task_done_sem) {
        xSemaphoreGive(ctx->task_done_sem);
    }
    vTaskDelete(NULL);
}

/* ── Driver vtable implementation ── */

static void *csi_init(const void *platform_config)
{
    const board_pipeline_csi_config_t *cfg =
        (const board_pipeline_csi_config_t *)platform_config;

    csi_driver_ctx_t *ctx = calloc(1, sizeof(csi_driver_ctx_t));
    if (!ctx) return NULL;
    ctx->video_fd = -1;
    ctx->ae_target = cfg->ae_target; /* 0 = use ISP default */
    ctx->hmirror = cfg->hmirror;
    ctx->vflip = cfg->vflip;

    /* Initialize esp_video with CSI config.
     * If an I2C bus handle is provided, reuse it for SCCB.
     * Otherwise, let esp_video init its own SCCB bus from pin config. */
    esp_video_init_csi_config_t csi_config[1];
    memset(csi_config, 0, sizeof(csi_config));
    csi_config[0].reset_pin = -1;
    csi_config[0].pwdn_pin = -1;

    if (cfg->i2c_bus) {
        csi_config[0].sccb_config.init_sccb = false;
        csi_config[0].sccb_config.i2c_handle = cfg->i2c_bus;
        csi_config[0].sccb_config.freq = 400000;
    } else {
        csi_config[0].sccb_config.init_sccb = true;
        csi_config[0].sccb_config.i2c_config.port = cfg->sccb_i2c_port;
        csi_config[0].sccb_config.i2c_config.sda_pin = cfg->sccb_sda_pin;
        csi_config[0].sccb_config.i2c_config.scl_pin = cfg->sccb_scl_pin;
        csi_config[0].sccb_config.freq = 100000;
    }
    esp_video_init_config_t cam_config = { .csi = csi_config };

    /* Init once per boot; never deinit (see s_esp_video_inited note above).
     * A second esp_video_init() would fail registering the already-present
     * ISP/CSI devices, so skip it on re-arm — the devices persist and the
     * fresh open()/REQBUFS below re-attaches this driver instance to them. */
    if (!s_esp_video_inited) {
        esp_err_t err = esp_video_init(&cam_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(err));
            free(ctx);
            return NULL;
        }
        s_esp_video_inited = true;
#if CSI_HAS_IPA
        /* Must follow esp_video_init() (the sensor and the CSI/ISP video devices
         * have to exist) and precede the open() below only by convention — this
         * mirrors where esp_video creates the pipeline in its own init path. */
        csi_ipa_start();
#endif
        /* After the IPA, so a tuning file that drives GAMMA itself sets the curve
         * first and this overrides it deliberately rather than racing it. */
        csi_apply_tone_curve();
        csi_apply_color();
    }

    /* Open V4L2 device */
    ctx->video_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (ctx->video_fd < 0) {
        ESP_LOGE(TAG, "Failed to open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        free(ctx);
        return NULL;
    }

    /* Query format (ISP delivers RGB565) */
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    if (ioctl(ctx->video_fd, VIDIOC_G_FMT, &fmt) < 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed: errno=%d", errno);
        goto fail;
    }
    ctx->frame_w = fmt.fmt.pix.width;
    ctx->frame_h = fmt.fmt.pix.height;
    ctx->frame_buf_size = ctx->frame_w * ctx->frame_h * 2; /* RGB565 */

    ESP_LOGI(TAG, "Camera format: %dx%d (pixfmt=0x%08"PRIx32", %zu bytes/frame)",
             ctx->frame_w, ctx->frame_h, fmt.fmt.pix.pixelformat,
             ctx->frame_buf_size);

    /* Sensor readout orientation. A board sets hmirror/vflip when its module reads
     * out flipped vs the pipeline geometry — e.g. the Guition JC4880P443's OV02C10
     * ships as a selfie camera (horizontally mirrored by default); one HFLIP cancels
     * it so it reads like the OV5647. Applied before streaming, only for a flip the
     * board asks for, so a sensor whose correct orientation relies on its own
     * defaults is untouched. NB: on OV02C10 the HORIZONTAL mirror (0x3821, column
     * reorder) is clean but a VERTICAL flip (0x3820) corrupts the frame geometry —
     * use BOARD_CAMERA_MIRROR_Y (PPA) for a vertical correction instead. */
    if (ctx->hmirror) {
        struct v4l2_ext_control c = { .id = V4L2_CID_HFLIP, .value = 1 };
        struct v4l2_ext_controls cs = {
            .ctrl_class = V4L2_CTRL_CLASS_USER, .count = 1, .controls = &c };
        if (ioctl(ctx->video_fd, VIDIOC_S_EXT_CTRLS, &cs) < 0) {
            ESP_LOGW(TAG, "Set sensor HMIRROR failed: errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "Sensor HMIRROR enabled");
        }
    }
    if (ctx->vflip) {
        struct v4l2_ext_control c = { .id = V4L2_CID_VFLIP, .value = 1 };
        struct v4l2_ext_controls cs = {
            .ctrl_class = V4L2_CTRL_CLASS_USER, .count = 1, .controls = &c };
        if (ioctl(ctx->video_fd, VIDIOC_S_EXT_CTRLS, &cs) < 0) {
            ESP_LOGW(TAG, "Set sensor VFLIP failed: errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "Sensor VFLIP enabled");
        }
    }

    /* Allocate frame buffers in PSRAM (cache-aligned for DMA) */
    const size_t cache_line = 128;
    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        ctx->frame_bufs[i] = heap_caps_aligned_calloc(
            cache_line, 1, ctx->frame_buf_size, MALLOC_CAP_SPIRAM);
        if (!ctx->frame_bufs[i]) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer %d", i);
            goto fail;
        }
    }

    /* Request V4L2 buffers (USERPTR mode) */
    struct v4l2_requestbuffers req = {
        .count  = CSI_NUM_BUFS,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_USERPTR,
    };
    if (ioctl(ctx->video_fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        goto fail;
    }

    /* Queue all buffers */
    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        struct v4l2_buffer buf = {
            .type    = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory  = V4L2_MEMORY_USERPTR,
            .index   = i,
            .m.userptr = (unsigned long)ctx->frame_bufs[i],
            .length  = ctx->frame_buf_size,
        };
        if (ioctl(ctx->video_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%d] failed", i);
            goto fail;
        }
    }

    ESP_LOGI(TAG, "CSI driver initialized (%dx%d, %d buffers)",
             ctx->frame_w, ctx->frame_h, CSI_NUM_BUFS);
    return ctx;

fail:
    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        if (ctx->frame_bufs[i]) heap_caps_free(ctx->frame_bufs[i]);
    }
    if (ctx->video_fd >= 0) close(ctx->video_fd);
    free(ctx);
    return NULL;
}

static esp_err_t csi_start(void *handle, cam_pipeline_frame_cb_t frame_cb,
                           void *user_ctx, int core_id)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)handle;
    ctx->frame_cb = frame_cb;
    ctx->user_ctx = user_ctx;
    ctx->running = true;

    /* Start V4L2 streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->video_fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed: errno=%d", errno);
        return ESP_FAIL;
    }

    /* Done-semaphore so csi_stop can join the capture task before STREAMOFF
     * (see csi_stop). Created before the task so the task can always give it. */
    ctx->task_done_sem = xSemaphoreCreateBinary();
    if (!ctx->task_done_sem) {
        ESP_LOGE(TAG, "Failed to create capture task done semaphore");
        ctx->running = false;
        ioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type);
        return ESP_FAIL;
    }

    /* Spawn capture task */
    BaseType_t ret = xTaskCreatePinnedToCore(
        csi_capture_task, "csi_cap", CSI_TASK_STACK, ctx,
        CSI_TASK_PRIORITY, &ctx->task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        vSemaphoreDelete(ctx->task_done_sem);
        ctx->task_done_sem = NULL;
        ctx->running = false;
        ioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type);
        return ESP_FAIL;
    }

    /* Apply the fixed exposure if configured (0 = leave it to the sensor/ISP).
     * Skipped when an IPA is running: this writes V4L2_CID_EXPOSURE once at
     * stream start, and the AGC loop drives the same control every few frames,
     * so the fixed value would simply be overwritten. Setting both is a config
     * mistake worth surfacing. */
    if (ctx->ae_target > 0) {
#if CSI_HAS_IPA
        if (s_ipa_started) {
            ESP_LOGW(TAG, "Ignoring fixed AE target %u: adaptive AE owns exposure "
                          "(clear CONFIG_BOARD_CSI_AE_TARGET for this board)",
                     ctx->ae_target);
        } else
#endif
        {
            csi_set_ae_target(ctx, ctx->ae_target);
        }
    }

    ESP_LOGI(TAG, "Streaming started (capture task on core %d)", core_id);
    return ESP_OK;
}

static esp_err_t csi_stop(void *handle)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)handle;

    /* Join the capture task BEFORE stopping the stream.
     *
     * The task blocks in VIDIOC_DQBUF, which esp_video waits on with a hardcoded
     * portMAX_DELAY and NEVER signals on stop -- VIDIOC_STREAMOFF actually
     * *deletes* the semaphore DQBUF is waiting on. So a task still parked in
     * DQBUF when we STREAMOFF is stranded forever, and it can't even be safely
     * vTaskDelete'd afterward (its event-list item points into the freed
     * semaphore, so removing it would corrupt the heap). Every scan cycle used to
     * orphan one such task, leaking its CSI_TASK_STACK (~16 KB internal RAM) until
     * a later cycle could no longer allocate a task stack and start() failed.
     *
     * Instead, clear `running` and wait for the task to exit on its own while the
     * stream is STILL ON. Frames keep arriving, so DQBUF returns within ~one frame
     * period, the loop sees running==false, gives task_done_sem, and self-deletes
     * cleanly. Only then do we STREAMOFF -- no task is parked in DQBUF, so esp_video
     * deleting the semaphore is harmless. */
    ctx->running = false;

    if (ctx->task_handle) {
        if (ctx->task_done_sem &&
            xSemaphoreTake(ctx->task_done_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
            /* Only reached if frames stopped arriving (camera stall) so DQBUF
             * never returned. Rare degraded case -- leave the task rather than
             * risk a heap-corrupting delete; it is not the per-cycle leak. */
            ESP_LOGW(TAG, "Capture task did not exit before STREAMOFF");
        }
        ctx->task_handle = NULL;
    }

    /* Stop V4L2 streaming (safe now: no task is blocked in DQBUF). */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type);

    if (ctx->task_done_sem) {
        vSemaphoreDelete(ctx->task_done_sem);
        ctx->task_done_sem = NULL;
    }

    ESP_LOGI(TAG, "Streaming stopped");
    return ESP_OK;
}

static void csi_deinit(void *handle)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)handle;
    if (!ctx) return;

    /* Release only this instance's V4L2 session (fd + buffers). The esp_video
     * device registrations stay up by design (see s_esp_video_inited) so the
     * next cam_pipeline_create() can re-open the device without a reboot. */
    for (int i = 0; i < CSI_NUM_BUFS; i++) {
        if (ctx->frame_bufs[i]) heap_caps_free(ctx->frame_bufs[i]);
    }
    if (ctx->video_fd >= 0) close(ctx->video_fd);
    free(ctx);

    ESP_LOGI(TAG, "CSI driver deinitialized (esp_video kept registered)");
}

static esp_err_t csi_get_resolution(void *handle, uint32_t *width,
                                    uint32_t *height)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)handle;
    if (!ctx) return ESP_ERR_INVALID_STATE;
    *width = ctx->frame_w;
    *height = ctx->frame_h;
    return ESP_OK;
}

static esp_err_t csi_set_ae_target(void *handle, uint32_t level)
{
    csi_driver_ctx_t *ctx = (csi_driver_ctx_t *)handle;
    if (!ctx || ctx->video_fd < 0) return ESP_ERR_INVALID_STATE;

    struct v4l2_ext_control control = {
        .id = V4L2_CID_EXPOSURE,
        .value = (int32_t)level,
    };
    struct v4l2_ext_controls controls = {
        .ctrl_class = V4L2_CTRL_CLASS_USER,
        .count = 1,
        .controls = &control,
    };
    if (ioctl(ctx->video_fd, VIDIOC_S_EXT_CTRLS, &controls) < 0) {
        ESP_LOGW(TAG, "Set AE target %"PRIu32" failed: errno=%d", level, errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AE target set to %"PRIu32, level);
    return ESP_OK;
}

const cam_pipeline_camera_driver_t board_pipeline_csi_driver = {
    .init           = csi_init,
    .start          = csi_start,
    .stop           = csi_stop,
    .deinit         = csi_deinit,
    .get_resolution = csi_get_resolution,
    .set_ae_target  = csi_set_ae_target,
    .set_focus      = NULL,
    .has_focus_motor = NULL,
};

#endif /* BOARD_HAS_CAMERA && CAMERA_CSI */
