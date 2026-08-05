#include "board.h"
#include "board_backlight.h"
#include "board_config.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "backlight";

#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define BL_LEDC_DUTY        (1024)
#define BL_LEDC_FREQUENCY   (5000)

#ifndef BOARD_BACKLIGHT_INVERTED
#define BOARD_BACKLIGHT_INVERTED 0
#endif

#ifndef BOARD_BACKLIGHT_DRIVER
#define BOARD_BACKLIGHT_DRIVER BACKLIGHT_LEDC
#endif

static uint8_t g_brightness = 0;

#if BOARD_BACKLIGHT_DRIVER == BACKLIGHT_COMPANION

/* Boards with no backlight GPIO: brightness is a PWM-duty register write to the
 * companion MCU, which owns the LED driver's enable pin. The bl_pin argument is
 * meaningless here and ignored.
 *
 * ORDERING: this needs a live I2C bus, so unlike the LEDC path it cannot run at
 * the very top of board_init(). board_init() calls it after the bus and the
 * companion MCU are up — which means the panel is dark for the first moments of
 * bring-up regardless of BOARD_BACKLIGHT_KEEP_ON_AT_BOOT. */
#include "board_stc8.h"

void board_backlight_init(int bl_pin)
{
    (void)bl_pin;
    if (!board_stc8_ready()) {
        ESP_LOGE(TAG, "companion MCU not ready — backlight will stay dark");
        return;
    }
    /* Explicitly park it off: the LED driver's enable is pulled low in hardware,
     * but the companion MCU survives a SoC reset with its previous duty, so a
     * warm reboot would otherwise keep the old brightness through bring-up. */
    board_stc8_set_pwm(BOARD_STC8_PWM_LCD_BL, 0);
    g_brightness = 0;
}

void board_backlight_set(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
        ESP_LOGE(TAG, "Brightness value out of range");
    }

    g_brightness = brightness;
    board_stc8_set_pwm(BOARD_STC8_PWM_LCD_BL, brightness);

    ESP_LOGI(TAG, "LCD brightness set to %d%%", brightness);
}

#else /* BACKLIGHT_LEDC */

void board_backlight_init(int bl_pin)
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = BL_LEDC_MODE;
    timer_cfg.timer_num = BL_LEDC_TIMER;
    timer_cfg.duty_resolution = BL_LEDC_DUTY_RES;
    timer_cfg.freq_hz = BL_LEDC_FREQUENCY;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.speed_mode = BL_LEDC_MODE;
    ch_cfg.channel = BL_LEDC_CHANNEL;
    ch_cfg.timer_sel = BL_LEDC_TIMER;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.gpio_num = bl_pin;
    /* Start with backlight OFF. On inverted boards duty=0 is full brightness,
     * so we need max duty to keep it dark until the first frame is rendered. */
#if BOARD_BACKLIGHT_INVERTED
    ch_cfg.duty = BL_LEDC_DUTY - 1;
#else
    ch_cfg.duty = 0;
#endif
    ch_cfg.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

void board_backlight_set(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
        ESP_LOGE(TAG, "Brightness value out of range");
    }

    g_brightness = brightness;
    uint32_t duty = (brightness * (BL_LEDC_DUTY - 1)) / 100;
#if BOARD_BACKLIGHT_INVERTED
    duty = (BL_LEDC_DUTY - 1) - duty;
#endif

    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));

    ESP_LOGI(TAG, "LCD brightness set to %d%%", brightness);
}

#endif /* BOARD_BACKLIGHT_DRIVER */

uint8_t board_backlight_get(void)
{
    return g_brightness;
}
