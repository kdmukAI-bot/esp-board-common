/**
 * STC8H1K companion-MCU driver (I2C).
 *
 * Some boards hang peripheral control lines off a small 8051 companion MCU
 * rather than SoC GPIOs. It is reached over the main I2C bus and exposes a flat
 * register file: one register per controllable line.
 *
 * Conceptually the same role as the TCA9554 I/O expander
 * (BOARD_HAS_IO_EXPANDER), but wider — it also carries PWM outputs and battery
 * telemetry, so it cannot be modelled as a plain expander.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output lines, in the companion MCU's own index order. The register address is
 * BOARD_STC8_REG_SET_GPIO + index, so these values are firmware ABI — do not
 * reorder. Names follow the vendor BSP's EM_STC8_GPIO_OUT. */
#define BOARD_STC8_OUT_TP_RST        0  /* touch panel reset      */
#define BOARD_STC8_OUT_CSI_RST       1  /* camera reset           */
#define BOARD_STC8_OUT_AUDIO_SD      2  /* audio amp shutdown     */
#define BOARD_STC8_OUT_LCD_BL_POWER  3  /* backlight rail enable  */

/* Input lines (BOARD_STC8_REG_GET_GPIO + index). */
#define BOARD_STC8_IN_SW_SPI_UART    0  /* UART/SPI slide switch  */

/* PWM channels (BOARD_STC8_REG_SET_PWM + index). */
#define BOARD_STC8_PWM_LCD_BL        0  /* backlight brightness   */

/**
 * Register the companion MCU on an existing I2C bus.
 * Safe to call more than once; later calls are no-ops.
 */
esp_err_t board_stc8_init(i2c_master_bus_handle_t bus);

/** True once board_stc8_init() has succeeded. */
bool board_stc8_ready(void);

/** Drive one of the BOARD_STC8_OUT_* lines high (1) or low (0). */
esp_err_t board_stc8_set_gpio(uint8_t out_index, uint8_t level);

/** Read one of the BOARD_STC8_IN_* lines. */
esp_err_t board_stc8_get_gpio(uint8_t in_index, uint8_t *level);

/** Set a PWM channel's duty cycle, 0-100 (percent). */
esp_err_t board_stc8_set_pwm(uint8_t pwm_index, uint8_t duty_pct);

#ifdef __cplusplus
}
#endif
