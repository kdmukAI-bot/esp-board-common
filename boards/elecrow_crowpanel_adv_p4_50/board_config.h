/**
 * Board configuration: Elecrow CrowPanel Advance 5" ESP32-P4
 *
 * Display:  parallel RGB, 800x480, LANDSCAPE-NATIVE (no panel controller IC)
 * Touch:    GT911 I2C
 * Camera:   2 MP MIPI-CSI on a 24-pin FPC (optional add-on module)
 * SoC:      ESP32-P4 (dual-core RISC-V 400MHz, 32MB PSRAM, 16MB flash)
 *
 * Two structural differences from every other board here:
 *
 *  1. The panel is landscape-native, so there is NO per-frame rotation — the
 *     cost that makes the portrait DSI boards expensive. board_display_rgb.c
 *     deliberately does not reuse any of the ST7701 rotation machinery.
 *
 *  2. Several control lines are registers on an STC8H1K companion MCU reached
 *     over I2C, not SoC GPIOs. The backlight is one of them, so the panel stays
 *     dark until that driver works (board_stc8.c).
 *
 * Pin assignments traced from the vendor Eagle netlist
 * (docs/board-schematics/elecrow-crowpanel-p4/), cross-checked against the
 * factory BSP. Where the two disagree the netlist wins — see the touch-reset
 * and backlight notes below.
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Elecrow CrowPanel Advance 5in P4"

/* ── Display (parallel RGB) ── */
/* 800x480 is the PHYSICAL orientation and also the logical one: this panel is
 * landscape-native. CONFIG_BOARD_LANDSCAPE is deliberately NOT set for this
 * board — that flag means "rotate a portrait panel", which would be wrong here
 * and would leave BOARD_DISP_*_RES swapped. */
#define BOARD_DISPLAY_DRIVER    DISPLAY_RGB
#define BOARD_LCD_H_RES         800
#define BOARD_LCD_V_RES         480

/* RGB timing. 25 MHz pclk gives ~60 Hz at this resolution and blanking. */
#define BOARD_RGB_PCLK_HZ       (25 * 1000 * 1000)
#define BOARD_RGB_HSYNC         4
#define BOARD_RGB_HBP           8
#define BOARD_RGB_HFP           8
#define BOARD_RGB_VSYNC         4
#define BOARD_RGB_VBP           16
#define BOARD_RGB_VFP           16

/* TWO PSRAM framebuffers (768 KB each at 800x480x2) + a 20-line bounce buffer.
 * 1.5 MB against 32 MB of PSRAM, and it REPLACES rather than adds: LVGL now
 * renders straight into these instead of into its own ~188 KB of scratch draw
 * buffers, so nothing lands in internal RAM either way.
 *
 * Why two. With one framebuffer LVGL had to render into scratch and the flush
 * then copied that region into the buffer the peripheral was scanning — every
 * full-screen repaint paid ~768 KB of rendering PLUS ~768 KB of PSRAM-to-PSRAM
 * copy, while the panel was already re-reading the whole framebuffer ~59 times a
 * second. Measured cost: 8 flushes and 28-70 ms per full repaint.
 *
 * With two, LVGL renders directly into the off-screen framebuffer and the flush
 * hands its address to esp_lcd_panel_draw_bitmap, which recognises the pointer as
 * one of its own framebuffers and simply switches cur_fb_index (see
 * esp_lcd_panel_rgb.c) — a pointer swap instead of a copy, and tear-free because
 * the switch takes effect on the next scan. */
/* ⚠ HELD AT 1 — the direct-mode path below it works and is much faster, but is NOT
 * yet safe. Do not raise this to 2 without reading the following.
 *
 * Measured, same build, only this macro differing, 5 boots x 13 s each:
 *   1 (partial): 0 panics.  full repaint = 8 flushes, avg render 23 ms
 *   2 (direct):  3 panics.  full repaint = 1 flush,  avg render  8 ms
 * So direct mode is a ~3x render win AND it destabilises something: the panics are
 * load faults inside mp_map_lookup() / mp_load_method() on the MicroPython task —
 * i.e. the MP heap, not the display. Suspected same root cause as an intermittent
 * "NameError: name 'X' isn't defined" seen in the app, which is what a corrupted
 * map lookup looks like when it merely misses instead of faulting.
 *
 * Ruled out already: the 0,0,800x480 bounds are correct for this landscape-native
 * panel; IDF does support num_fbs=2 alongside a bounce buffer; the buffer size
 * handed to LVGL (800*480*2) matches IDF's own fb_size.
 * Next suspect to test: the flush passes the FULL screen to
 * esp_lcd_panel_draw_bitmap(), so every frame triggers a 768 KB cache write-back;
 * scoping that to the dirty area is the next experiment. */
#define BOARD_RGB_NUM_FBS               1
#define BOARD_RGB_BOUNCE_BUFFER_LINES   20

/* Sync + data pins. The 16 data lines are RGB565 in panel bit order:
 * DATA0-4 = B3-B7, DATA5-10 = G2-G7, DATA11-15 = R3-R7. */
#define BOARD_PIN_RGB_HSYNC     GPIO_NUM_40
#define BOARD_PIN_RGB_VSYNC     GPIO_NUM_41
#define BOARD_PIN_RGB_DE        GPIO_NUM_2
#define BOARD_PIN_RGB_PCLK      GPIO_NUM_3

#define BOARD_PIN_RGB_DATA0     GPIO_NUM_8
#define BOARD_PIN_RGB_DATA1     GPIO_NUM_7
#define BOARD_PIN_RGB_DATA2     GPIO_NUM_6
#define BOARD_PIN_RGB_DATA3     GPIO_NUM_5
#define BOARD_PIN_RGB_DATA4     GPIO_NUM_4
#define BOARD_PIN_RGB_DATA5     GPIO_NUM_14
#define BOARD_PIN_RGB_DATA6     GPIO_NUM_13
#define BOARD_PIN_RGB_DATA7     GPIO_NUM_12
#define BOARD_PIN_RGB_DATA8     GPIO_NUM_11
#define BOARD_PIN_RGB_DATA9     GPIO_NUM_10
#define BOARD_PIN_RGB_DATA10    GPIO_NUM_9
#define BOARD_PIN_RGB_DATA11    GPIO_NUM_19
#define BOARD_PIN_RGB_DATA12    GPIO_NUM_18
#define BOARD_PIN_RGB_DATA13    GPIO_NUM_17
#define BOARD_PIN_RGB_DATA14    GPIO_NUM_16
#define BOARD_PIN_RGB_DATA15    GPIO_NUM_15

/* The panel's DISP pin is strapped high through a 10K pull-up — no software
 * control, and none needed. */
#define BOARD_PIN_RGB_DISP      -1

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              0
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0
#define BOARD_DISPLAY_DIRECT_MODE       0
#define BOARD_DISPLAY_INVERT_COLOR      0

/* ── Backlight (via companion MCU) ── */
/* There is no backlight GPIO at all: the MT9201 boost driver's enable pin is
 * owned by the STC8, so brightness is an I2C register write. BOARD_PIN_LCD_BL
 * exists only to satisfy the shared board_backlight_init() signature.
 *
 * Note what the vendor BSP's enum implies but this hardware does not have: its
 * STC8_GPIO_OUT_LCD_BL_POWER is a dead line on this revision — the switching
 * FET is depopulated and a 0R link ties the LED rail permanently to 5V. The PWM
 * enable is the only real control, and the factory firmware never touches the
 * power line either. */
#define BOARD_BACKLIGHT_DRIVER      BACKLIGHT_COMPANION
#define BOARD_PIN_LCD_BL            (-1)
#define BOARD_BACKLIGHT_INVERTED    0
/* Off through bring-up. The LED driver's enable is pulled low in hardware, so
 * the panel is genuinely dark from power-up and there is no transient to hide
 * behind — the caller raises the backlight once the boot logo has rendered. */
#define BOARD_BACKLIGHT_KEEP_ON_AT_BOOT  0

/* ── Companion MCU (STC8H1K, 8051 over I2C) ── */
/* Owns the backlight PWM, the camera reset, and the audio amp shutdown. Shares
 * the main I2C bus with the touch controller — the two sit on opposite sides of
 * a level shifter but are electrically the same bus.
 *
 * Its reset line is tied to the SoC's CHIP_PU, so it reboots with the P4: any
 * state written to it (notably brightness) must be re-established after a reset
 * rather than assumed to persist. */
#define BOARD_HAS_COMPANION_MCU     1
#define BOARD_COMPANION_MCU_ADDR    0x2F

/* ── IO Expander ── */
#define BOARD_HAS_IO_EXPANDER   0

/* ── Touch (GT911) ── */
/* RST is a real SoC GPIO despite the companion MCU also exposing a TP_RST line:
 * the netlist routes GPIO36 through three populated 0R links to the touch FPC,
 * while the companion MCU's branch to the same net is depopulated. The vendor
 * BSP agrees (it uses GPIO36 and never calls the companion's TP_RST).
 *
 * The INT pull-up is depopulated, so firmware fully owns the address strap
 * during the reset pulse; the driver probes both 0x5D and 0x14. */
#define BOARD_TOUCH_DRIVER      TOUCH_GT911
#define BOARD_PIN_TOUCH_RST     GPIO_NUM_36
#define BOARD_PIN_TOUCH_INT     GPIO_NUM_42

/* ── I2C ── */
/* Main bus: touch + companion MCU. The camera's SCCB is a SEPARATE bus on this
 * board (GPIO33/34 at 1.8V) — see the camera section. */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_45
#define BOARD_PIN_I2C_SCL       GPIO_NUM_46
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
/* No PMIC. Battery telemetry (charge state, level) is available from the
 * companion MCU instead; unused so far. */
#define BOARD_HAS_PMIC          0

/* ── LVGL port tuning ── */
/* Flattened to 1 (== MicroPython VM task) so LVGL-lock access is FIFO-fair and
 * the prio-1 VM/consumer doesn't starve — same rationale as the other P4
 * boards. */
#define BOARD_LVGL_TASK_PRIORITY    1
#define BOARD_LVGL_TASK_STACK       (1024 * 16)
/* Stack in INTERNAL RAM, overriding the fleet's PSRAM default. Device-confirmed
 * necessary here: with the stack in PSRAM this board panicked on four consecutive
 * boots, always in taskLVGL, and the coredump showed its registers reading back as
 * zeros — a PSRAM stack being read while a flash write had the cache disabled.
 * Costs 16 KB of internal RAM, which is deliberate and small; the fragmentation
 * work that pushed big allocations to PSRAM is unaffected (the ~188 KB of LVGL
 * draw buffers and the camera's buffers all stay in PSRAM). */
#define BOARD_LVGL_TASK_STACK_CAPS  MALLOC_CAP_INTERNAL
#define BOARD_LVGL_TASK_AFFINITY    -1  /* No core affinity */
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── Camera (MIPI-CSI, SC2336) ── */
/* The sensor is a SmartSens SC2336 (2 MP 1280x720): SCCB answers at 0x30 and
 * chip-ID 0x3107/0x3108 reads 0xCB3A. The driver ships in esp_cam_sensor, so
 * unlike the Guition's OV02C10 there is no add-on component; it is selected by
 * CONFIG_CAMERA_SC2336 in this board's sdkconfig.board.
 *
 * Four things differ from the rest of the fleet:
 *   - SCCB is its own I2C bus, not the main one. The P4 pins are ordinary 3.3 V
 *     IO (pulled up to VDDPST_5); BSS138 shifters Q6/Q7, gated on DOVDD_1V8, put
 *     only the SENSOR side at 1.8 V, so nothing here needs a special IO voltage.
 *   - the sensor reset is a companion-MCU line (STC8 P1.3), not a GPIO — and the
 *     companion MCU releases it by default, so firmware never has to touch it
 *     (the sensor answers SCCB from a cold boot).
 *   - XVCLK comes from a dedicated 24 MHz oscillator, so no XCLK pin is driven.
 *   - the sensor is RAW-Bayer with manual exposure/gain, so it needs a closed
 *     loop for AE/AWB. Unlike the OV02C10 this one comes for free: esp_cam_sensor
 *     ships cfg/sc2336_default.json keyed "SC2336", which is the sensor's own
 *     reported name, so esp_video's lookup hits it and builds the ISP pipeline
 *     itself. BOARD_CAMERA_IPA_CONFIG_NAME is therefore deliberately NOT defined
 *     here — defining it would take ownership of the pipeline (for per-session AE
 *     metering) and require our own copy of the tuning file.
 */
#ifndef BOARD_HAS_CAMERA
#define BOARD_HAS_CAMERA            1
#endif
#define BOARD_CAMERA_INTERFACE      CAMERA_CSI
#define BOARD_PIN_CAM_SCCB_SDA      GPIO_NUM_33
#define BOARD_PIN_CAM_SCCB_SCL      GPIO_NUM_34
#define BOARD_CAM_SCCB_I2C_PORT     1   /* Dedicated bus — NOT the main I2C */

/* Camera orientation. This panel is landscape-native, so unlike the DSI boards there
 * is no display-rotation term folded in — board_pipeline.c passes BOARD_CAMERA_ROTATION
 * through unchanged, and the value here is a pure camera-to-panel MOUNT offset.
 *
 * 180: device-observed. The module is mounted inverted relative to the panel, so the
 * preview came up upside down with no rotation applied. No mirror correction is
 * applied — the image reads the right way round, it was only inverted, which a 180
 * rotation alone fixes (a mirrored mount would need BOARD_CAMERA_MIRROR_Y as well,
 * and on a RAW-Bayer sensor that correction must go through the PPA, not the sensor's
 * own flip registers — see the Guition notes on Bayer-phase damage). */
#define BOARD_CAMERA_ROTATION       180

/* Image-entropy still: a centred SQUARE. The frame is 1280x720, so 720 is the
 * largest square the sensor can supply; anything bigger would force the grab to
 * UPSCALE, which the PPA rejects ("scale does not fit in the out pic") and the
 * capture hangs. P4 only (needs PPA). */
#define BOARD_ENTROPY_STILL_DIM     720

/* ── SD Card (1-bit SDMMC) ── */
/* Same CLK/CMD/D0 pins as the rest of the P4 fleet, but ONE bit wide: DAT1-3
 * terminate on pull-ups at the socket and are not routed to the SoC, so a
 * 4-bit configuration cannot work here. */
#ifndef BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            1
#endif
#define BOARD_SD_WIDTH              1
#define BOARD_PIN_SD_CLK            GPIO_NUM_43
#define BOARD_PIN_SD_CMD            GPIO_NUM_44
#define BOARD_PIN_SD_D0             GPIO_NUM_39
/* No on-chip LDO for the SD rail on this board, unlike the rest of the P4 fleet.
 * The card's VDD comes straight off the board's VDD_3V3 (J5.VDD), and the SoC IO
 * domain carrying CLK/CMD/D0 (VDDPST_5) is bridged to that same rail by R25 (0R).
 * The LDO4 feedback link R109 is NC — the on-chip regulator's output reaches only a
 * decoupling cap. Acquiring an LDO channel here would regulate nothing, so 0. */
#define BOARD_SD_PWR_LDO_CHAN       0

/* ── Audio (NS4168) ── */
/* Amp shutdown is a companion-MCU line (BOARD_STC8_OUT_AUDIO_SD). Unused. */
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             0
#endif

/* ── RTC / IMU ── */
#ifndef BOARD_HAS_RTC
#define BOARD_HAS_RTC               0
#endif
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU               0
#endif

/* ── Radio co-processor (ESP32-C6-MINI-1) ── */
/* GPIO20, NOT the GPIO54 used by every other board here. On this board GPIO54
 * is SD2_CMD — the SDIO data link TO the C6 — so copying the fleet's value
 * would drive a bus line instead of a reset.
 *
 * GPIO20 drives the C6's EN pin through a 0R series resistor and has no other
 * connection anywhere on the board. EN is pulled high, so the C6 runs by
 * default and board_init() drives this low and leaves it low.
 *
 * Residual exposure worth knowing: GPIO20 is high-Z while the SoC is in reset,
 * so the C6 does execute its own flash for the moments before this runs. Only
 * removing R76 — the single 0R link feeding the C6's 3V3 rail — makes the air
 * gap absolute. See the hardware-kb entry for this board. */
#define BOARD_RADIO_COPROC_RESET_PIN GPIO_NUM_20
