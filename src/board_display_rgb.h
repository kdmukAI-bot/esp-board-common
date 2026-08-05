/**
 * Parallel RGB display driver — see board_display_rgb.c.
 */
#pragma once

#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and start the RGB panel.
 *
 * Unlike the SPI/QSPI paths there is no panel-IO handle: an RGB panel has no
 * command channel, so the caller's io_handle is set to NULL.
 */
void board_display_rgb_init(esp_lcd_panel_io_handle_t *io_handle,
                            esp_lcd_panel_handle_t *panel_handle);

#ifdef __cplusplus
}
#endif
