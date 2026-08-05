/**
 * Parallel RGB display driver (ESP32-P4 / ESP32-S3 LCD_CAM RGB interface).
 *
 * A raw RGB panel has no controller IC and no command channel: the SoC shifts
 * pixels out continuously against HSYNC/VSYNC/DE/PCLK, exactly like a monitor.
 * There is no init command sequence to send and no MADCTL to write — so unlike
 * every other display path here, orientation and colour order are fixed by the
 * panel's wiring and cannot be changed in software.
 *
 * That constraint is the point. Boards using this path have landscape-native
 * panels, so nothing needs rotating and none of the ST7701/DSI machinery
 * (deferred flush task, CPU rotation buffers, portrait-scan mode switch)
 * applies. Keep this path simple; resist generalising those gates into it.
 *
 * Timing and pin assignments come from board_config.h.
 */
#include "board.h"
#include "board_config.h"

#if BOARD_DISPLAY_DRIVER == DISPLAY_RGB

#include "board_display_rgb.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

static const char *TAG = "display_rgb";

/* Bounce buffers let the RGB peripheral DMA from internal RAM while the
 * framebuffer lives in PSRAM: the driver copies PSRAM -> bounce buffer -> LCD
 * FIFO, which keeps PSRAM bandwidth spikes from starving the pixel stream and
 * tearing the image. Sized in whole lines. */
#ifndef BOARD_RGB_BOUNCE_BUFFER_LINES
#define BOARD_RGB_BOUNCE_BUFFER_LINES 20
#endif

#ifndef BOARD_RGB_NUM_FBS
#define BOARD_RGB_NUM_FBS 2
#endif

/* No DISP enable line on boards seen so far (the panel's DISP pin is strapped
 * high in hardware), but keep it configurable. */
#ifndef BOARD_PIN_RGB_DISP
#define BOARD_PIN_RGB_DISP -1
#endif

void board_display_rgb_init(esp_lcd_panel_io_handle_t *io_handle,
                            esp_lcd_panel_handle_t *panel_handle)
{
    ESP_LOGI(TAG, "Initialising parallel RGB display (%dx%d @ %d MHz)",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES,
             BOARD_RGB_PCLK_HZ / 1000000);

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src        = LCD_CLK_SRC_DEFAULT,
        .data_width     = 16,               /* RGB565 */
        .bits_per_pixel = 16,
        .num_fbs        = BOARD_RGB_NUM_FBS,
        .bounce_buffer_size_px = BOARD_RGB_BOUNCE_BUFFER_LINES * BOARD_LCD_H_RES,
        .dma_burst_size = 64,
        .disp_gpio_num  = BOARD_PIN_RGB_DISP,
        .pclk_gpio_num  = BOARD_PIN_RGB_PCLK,
        .vsync_gpio_num = BOARD_PIN_RGB_VSYNC,
        .hsync_gpio_num = BOARD_PIN_RGB_HSYNC,
        .de_gpio_num    = BOARD_PIN_RGB_DE,
        .data_gpio_nums = {
            BOARD_PIN_RGB_DATA0,  BOARD_PIN_RGB_DATA1,
            BOARD_PIN_RGB_DATA2,  BOARD_PIN_RGB_DATA3,
            BOARD_PIN_RGB_DATA4,  BOARD_PIN_RGB_DATA5,
            BOARD_PIN_RGB_DATA6,  BOARD_PIN_RGB_DATA7,
            BOARD_PIN_RGB_DATA8,  BOARD_PIN_RGB_DATA9,
            BOARD_PIN_RGB_DATA10, BOARD_PIN_RGB_DATA11,
            BOARD_PIN_RGB_DATA12, BOARD_PIN_RGB_DATA13,
            BOARD_PIN_RGB_DATA14, BOARD_PIN_RGB_DATA15,
        },
        .timings = {
            .pclk_hz           = BOARD_RGB_PCLK_HZ,
            .h_res             = BOARD_LCD_H_RES,
            .v_res             = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_RGB_HSYNC,
            .hsync_back_porch  = BOARD_RGB_HBP,
            .hsync_front_porch = BOARD_RGB_HFP,
            .vsync_pulse_width = BOARD_RGB_VSYNC,
            .vsync_back_porch  = BOARD_RGB_VBP,
            .vsync_front_porch = BOARD_RGB_VFP,
            .flags = {
                .hsync_idle_low  = false,
                .vsync_idle_low  = false,
                .de_idle_high    = false,
                /* Latch data on the falling edge and idle PCLK high — the
                 * panel samples on the rising edge, so data must be stable
                 * before it. Getting this pair wrong shows as a sheared or
                 * colour-fringed image rather than a blank one. */
                .pclk_active_neg = true,
                .pclk_idle_high  = true,
            },
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));

    /* RGB panels have no command channel. */
    *io_handle = NULL;

    ESP_LOGI(TAG, "RGB display initialized (%d fbs, %d-line bounce buffer)",
             BOARD_RGB_NUM_FBS, BOARD_RGB_BOUNCE_BUFFER_LINES);
}

#endif /* BOARD_DISPLAY_DRIVER == DISPLAY_RGB */
