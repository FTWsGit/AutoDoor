#include "servo.h"

#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_check.h"

#define SERVO_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER   LEDC_TIMER_0
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_0
#define SERVO_LEDC_RES     LEDC_TIMER_13_BIT
#define SERVO_FREQ_HZ      50
#define SERVO_PERIOD_US    (1000000 / SERVO_FREQ_HZ)

static const char *TAG = "servo";
static bool s_ready;

static uint32_t pulse_to_duty(uint32_t pulse_us)
{
    return (pulse_us * (1U << SERVO_LEDC_RES)) / SERVO_PERIOD_US;
}

static bool pulse_in_range(uint32_t pulse_us)
{
    return pulse_us >= SERVO_MIN_PULSE_US && pulse_us <= SERVO_MAX_PULSE_US;
}

esp_err_t servo_init(const servo_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(!s_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->gpio), ESP_ERR_INVALID_ARG, TAG,
                        "GPIO %d cannot be used as output", config->gpio);
    ESP_RETURN_ON_FALSE(pulse_in_range(config->initial_pulse_us), ESP_ERR_INVALID_ARG, TAG,
                        "initial pulse %u us out of range", (unsigned)config->initial_pulse_us);

    const ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "LEDC timer config failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = config->gpio,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = pulse_to_duty(config->initial_pulse_us),
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "LEDC channel config failed");

    s_ready = true;
    return ESP_OK;
}

esp_err_t servo_set_pulse_us(uint32_t pulse_us)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(pulse_in_range(pulse_us), ESP_ERR_INVALID_ARG, TAG,
                        "pulse %u us out of range", (unsigned)pulse_us);

    ESP_RETURN_ON_ERROR(ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, pulse_to_duty(pulse_us)),
                        TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL), TAG,
                        "update duty failed");
    return ESP_OK;
}
