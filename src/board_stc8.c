/**
 * STC8H1K companion-MCU driver — see board_stc8.h.
 *
 * Protocol (from the vendor BSP): 7-bit I2C slave, one-byte register address,
 * one-byte payload. A write is the two-byte sequence {reg, value}; a read is a
 * write of {reg} followed by a read of the value. Register bases are grouped by
 * function and indexed by line number.
 *
 * Compiled only for boards that declare BOARD_HAS_COMPANION_MCU.
 */
#include "board_config.h"

#if defined(BOARD_HAS_COMPANION_MCU) && BOARD_HAS_COMPANION_MCU

#include "board_stc8.h"

#include "esp_log.h"

static const char *TAG = "stc8";

/* Register file bases. reg = base + line index. */
#define BOARD_STC8_REG_BATTERY   0x00
#define BOARD_STC8_REG_GET_GPIO  0x10
#define BOARD_STC8_REG_SET_GPIO  0x18
#define BOARD_STC8_REG_SET_PWM   0x20

#define BOARD_STC8_TIMEOUT_MS    1000

static i2c_master_dev_handle_t s_dev = NULL;

esp_err_t board_stc8_init(i2c_master_bus_handle_t bus)
{
    if (s_dev) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BOARD_COMPANION_MCU_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device 0x%02X failed: %s",
                 BOARD_COMPANION_MCU_ADDR, esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    ESP_LOGI(TAG, "companion MCU registered at 0x%02X", BOARD_COMPANION_MCU_ADDR);
    return ESP_OK;
}

bool board_stc8_ready(void)
{
    return s_dev != NULL;
}

static esp_err_t stc8_write_reg(uint8_t reg, uint8_t value)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), BOARD_STC8_TIMEOUT_MS);
}

esp_err_t board_stc8_set_gpio(uint8_t out_index, uint8_t level)
{
    esp_err_t err = stc8_write_reg(BOARD_STC8_REG_SET_GPIO + out_index, level ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set gpio %u failed: %s", out_index, esp_err_to_name(err));
    }
    return err;
}

esp_err_t board_stc8_get_gpio(uint8_t in_index, uint8_t *level)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (!level) return ESP_ERR_INVALID_ARG;

    uint8_t reg = BOARD_STC8_REG_GET_GPIO + in_index;
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, level, 1,
                                                BOARD_STC8_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get gpio %u failed: %s", in_index, esp_err_to_name(err));
    }
    return err;
}

esp_err_t board_stc8_set_pwm(uint8_t pwm_index, uint8_t duty_pct)
{
    if (duty_pct > 100) duty_pct = 100;

    esp_err_t err = stc8_write_reg(BOARD_STC8_REG_SET_PWM + pwm_index, duty_pct);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set pwm %u failed: %s", pwm_index, esp_err_to_name(err));
    }
    return err;
}

#endif /* BOARD_HAS_COMPANION_MCU */
