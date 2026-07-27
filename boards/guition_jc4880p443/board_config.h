/**
 * Board configuration: Guition JC4880P443 (JCZN / "JC")
 *
 * Display:  ST7701S MIPI-DSI 2-lane (480x800, portrait native)
 * Touch:    GT911 I2C
 * Camera:   OV02C10 MIPI-CSI 2-lane
 * SoC:      ESP32-P4 (dual-core RISC-V 400MHz, 32MB PSRAM, 16MB flash)
 *
 * A near-twin of the Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 (same SoC, same
 * ST7701S 480x800 MIPI-DSI panel class, same GT911 touch) from an independent
 * vendor. This is a board config + pin deltas, not a port: it reuses the
 * shared ST7701 + GT911 drivers. Pin assignments from the vendor schematic
 * (sheet 3_ESP32-P4) cross-checked against the ESPHome JC4880P443 preset.
 * Deltas vs the Waveshare twin are called out inline.
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Guition JC4880P443"

/* ── Display (MIPI-DSI, ST7701) ── */
#define BOARD_DISPLAY_DRIVER    DISPLAY_ST7701
#define BOARD_LCD_H_RES         480
#define BOARD_LCD_V_RES         800
#define BOARD_PIN_LCD_RST       GPIO_NUM_5   /* Waveshare twin: GPIO27 */
#define BOARD_PIN_LCD_BL        GPIO_NUM_23  /* Waveshare twin: GPIO26 */

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              0
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0
#define BOARD_DISPLAY_DIRECT_MODE       0
#define BOARD_DISPLAY_INVERT_COLOR      0

/* ── MIPI-DSI configuration ── */
/* Same ST7701S controller + 480x800 panel as the Waveshare twin, so start from
 * its DPI timing. If the panel won't sync, take the exact init/timing from the
 * vendor MIPI init or the ESPHome JC4880P443 preset (see the build plan doc). */
#define BOARD_MIPI_DSI_LANE_NUM             2
#define BOARD_MIPI_DSI_LANE_BITRATE_MBPS    500
#define BOARD_MIPI_DSI_PHY_LDO_CHAN         3
#define BOARD_MIPI_DSI_PHY_LDO_MV           2500
#define BOARD_MIPI_DPI_CLK_MHZ              30
#define BOARD_MIPI_DPI_NUM_FBS              3
/* DPI video timing */
#define BOARD_MIPI_DPI_HBP                  42
#define BOARD_MIPI_DPI_HSYNC                12
#define BOARD_MIPI_DPI_HFP                  42
#define BOARD_MIPI_DPI_VBP                  2
#define BOARD_MIPI_DPI_VSYNC                8
#define BOARD_MIPI_DPI_VFP                  60

/* ── Backlight ── */
/* NON-inverted on this board (Waveshare twin is inverted): higher LEDC duty =
 * brighter. board_backlight starts at duty 0 (off) and ramps up on set(). */
#define BOARD_BACKLIGHT_INVERTED    0
/* Keep the backlight lit from boot through the logo (avoids a mid-boot dip),
 * matching the Waveshare P4-43 twin.
 *
 * KNOWN COSMETIC ARTIFACT (deferred): on a COLD boot this board shows a ~3 s
 * light-blue flash before the boot logo. That is the ST7701S panel's own output
 * during the pre-initialisation window — after power-up but before
 * board_display_st7701_init() runs and starts streaming the (calloc'd, black)
 * DPI framebuffers. The twin's panel happens to show black in that same window;
 * this one shows blue. Toggling KEEP_ON does NOT change it (verified on device):
 * the window is before firmware controls the backlight or panel, and the
 * backlight is already on from hardware power-up. A real fix would need to assert
 * the panel reset (BOARD_PIN_LCD_RST) / force the backlight off from very early
 * boot (bootloader level); judged not worth it for a cosmetic cold-boot flash.
 * Cold-boot-only — warm/esptool resets don't reproduce it. */
#define BOARD_BACKLIGHT_KEEP_ON_AT_BOOT   1

/* ── IO Expander ── */
#define BOARD_HAS_IO_EXPANDER   0

/* ── Touch (GT911) ── */
/* Both RST and INT are wired on this board (the Waveshare twin leaves INT NC). */
#define BOARD_TOUCH_DRIVER      TOUCH_GT911
#define BOARD_PIN_TOUCH_RST     GPIO_NUM_22  /* Waveshare twin: GPIO23 */
#define BOARD_PIN_TOUCH_INT     GPIO_NUM_21  /* Waveshare twin: NC */

/* ── I2C ── */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_7
#define BOARD_PIN_I2C_SCL       GPIO_NUM_8
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
#define BOARD_HAS_PMIC          0

/* ── LVGL port tuning ── */
/* Flattened to 1 (== MicroPython VM task) so LVGL-lock access is FIFO-fair and the
 * prio-1 VM/consumer doesn't starve at overlay-create / present(). A/B-confirmed not
 * to affect preview fps (prio-5 firmware measured the same). See board_common note. */
#define BOARD_LVGL_TASK_PRIORITY    1
#define BOARD_LVGL_TASK_STACK       (1024 * 16)
#define BOARD_LVGL_TASK_AFFINITY    -1  /* No core affinity */
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── ST7701 deferred-flush task ──
 * The landscape flush does a ~30ms CPU rotation + vsync wait + panel blit. It
 * runs on this task so that work happens OFF the LVGL lock (see board_init.c).
 * Prio 1 == the flatten baseline: it must NOT preempt the lvgl render (which
 * holds the lock), and it does not take the LVGL mutex itself, so equal prio is
 * safe for mutex fairness. Core/priority are tuning knobs — if the flush task
 * gets starved (wait_for_flushing stalls under the lock), try raising the
 * priority or pinning to core 1. */
#define BOARD_ST7701_FLUSH_TASK_PRIORITY   1
#define BOARD_ST7701_FLUSH_TASK_STACK      4096
#define BOARD_ST7701_FLUSH_TASK_AFFINITY   -1  /* tskNO_AFFINITY */

/* ── Camera (MIPI-CSI, OV02C10) ── */
/* The sensor is OV02C10 (not the twin's OV5647). Its driver lives in the
 * standalone board_common/components/ov02c10 add-on and is selected by
 * CONFIG_CAMERA_OV02C10 in this board's sdkconfig.board. Like the twin, the
 * module self-clocks and needs no reset/pwdn/XCLK GPIOs (esp_video is called
 * with reset_pin/pwdn_pin = -1; XCLK is the P4 internal clock router). */
#ifndef BOARD_HAS_CAMERA
#define BOARD_HAS_CAMERA            1
#endif
#define BOARD_CAMERA_INTERFACE      CAMERA_CSI
#define BOARD_PIN_CAM_SCCB_SDA      GPIO_NUM_7
#define BOARD_PIN_CAM_SCCB_SCL      GPIO_NUM_8
#define BOARD_CAM_SCCB_I2C_PORT     0   /* Shares main I2C bus */

/* Camera orientation — two independent physical corrections:
 *
 * 1. Rotation. The camera flex is folded rearward (180° in-plane) to aim the lens
 *    away from the user, so the image is rotated 180° vs the twin's mount (which
 *    needs 0). Handled by BOARD_CAMERA_ROTATION.
 *
 * 2. Handedness. This OV02C10 module is wired as a SELFIE camera, so it reads out
 *    horizontally mirrored by default (device-confirmed: printed text renders
 *    backwards). This MUST be corrected in the PPA (post-ISP RGB565), not at the
 *    sensor: OV02C10 is a RAW-Bayer sensor, so toggling its flip registers shifts
 *    the Bayer phase and wrecks the fixed-GBRG ISP demosaic — HFLIP (0x3821) wipes
 *    the blue/green channels, VFLIP (0x3820) corrupts the frame geometry. The PPA
 *    mirror runs on already-demosaiced pixels, so it is clean. mirror_Y is the
 *    correct axis: in PPA-output space it lands as a horizontal flip on the live
 *    scan preview and a vertical flip on the app-rotated entropy still, cancelling
 *    each path's mirror at once (device-tuned across the full mx/my table).
 */
#define BOARD_CAMERA_ROTATION       180
#define BOARD_CAMERA_MIRROR_Y       1

/* Image-entropy still: a centred SQUARE, 720x720. A widescreen still can't fill the
 * landscape display on this board — device-proven: the camera PPA rotates 90° (this
 * DSI panel is portrait-native; board_pipeline pre-rotates the camera to the landscape
 * canvas), so the OV02C10's long (1288) axis maps to the display's SHORT axis and the
 * extra field of view is vertical, not horizontal. A square is the natural fit and is
 * rotation-invariant. 720 (not the old 960) is chosen because the frame is only 728
 * tall: a bigger square would force the still grab to UPSCALE, which the PPA rejects
 * ("scale does not fit in the out pic") and the capture hangs. P4 only (needs PPA). */
#define BOARD_ENTROPY_STILL_DIM     720

/* ── SD Card (4-bit SDMMC) ── */
/* Same pins as the Waveshare twin (39–44). SD power (TF_VCC) is default-on via a
 * P-FET, so no extra rail enable is needed here. */
#ifndef BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            1
#endif
#define BOARD_SD_WIDTH              4
#define BOARD_PIN_SD_CLK            GPIO_NUM_43
#define BOARD_PIN_SD_CMD            GPIO_NUM_44
#define BOARD_PIN_SD_D0             GPIO_NUM_39
#define BOARD_PIN_SD_D1             GPIO_NUM_40
#define BOARD_PIN_SD_D2             GPIO_NUM_41
#define BOARD_PIN_SD_D3             GPIO_NUM_42

/* ── Audio (ES8311 + ES7210) ── */
/* Disabled for bring-up. The codec I2S/PA pins need a trace pass (the LRCK vs
 * PA_CTRL GPIO10 assignment is ambiguous on the schematic) — see phase 3. */
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             0
#endif
#define BOARD_PIN_I2S_MCK           GPIO_NUM_13
#define BOARD_PIN_I2S_BCK           GPIO_NUM_12
#define BOARD_PIN_I2S_LRCK          GPIO_NUM_10
#define BOARD_PIN_I2S_DOUT          GPIO_NUM_9
#define BOARD_PIN_I2S_DIN           GPIO_NUM_11
#define BOARD_PIN_PA                GPIO_NUM_53

/* ── RTC / IMU ── */
#ifndef BOARD_HAS_RTC
#define BOARD_HAS_RTC               0
#endif
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU               0
#endif

/* ── Radio co-processor (ESP32-C6, SDIO slave) ── */
/* Same wiring as the Waveshare twin: the C6 reset idles high (runs its factory
 * hosted-slave firmware) and GPIO54 is the esp-hosted SDIO reset-slave pin
 * (low = held in reset). SeedSigner never uses the radio, so board_init() drives
 * this low at boot and leaves it low — the C6 executes no code (air gap). */
#define BOARD_RADIO_COPROC_RESET_PIN GPIO_NUM_54
