#include "servo.h"

#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_check.h"

/* 0..SERVO_MAX_ANGLE is mapped linearly onto this pulse range; the output never leaves it. */
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2500

#define SERVO_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER   LEDC_TIMER_0
#define SERVO_LEDC_CHANNEL LEDC_CHANNEL_0
#define SERVO_LEDC_RES     LEDC_TIMER_14_BIT
#define SERVO_FREQ_HZ      50
#define SERVO_PERIOD_US    (1000000 / SERVO_FREQ_HZ)

static const char *TAG = "servo";
static bool s_ready;

static uint32_t pulse_to_duty(uint32_t pulse_us)
{
    return (pulse_us * (1U << SERVO_LEDC_RES)) / SERVO_PERIOD_US;
}

static uint32_t angle_to_pulse_us(uint8_t angle_deg)
{
    const uint32_t span = SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US;
    /* Scale first, round once, then add the offset so 0 deg maps exactly to MAX_PULSE_US. */
    const uint32_t scaled = ((uint32_t)angle_deg * span + SERVO_MAX_ANGLE / 2) / SERVO_MAX_ANGLE;
    return SERVO_MAX_PULSE_US - scaled;
}

static esp_err_t drive_duty(uint32_t duty)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty), TAG,
                        "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL), TAG,
                        "update duty failed");
    return ESP_OK;
}

esp_err_t servo_init(const servo_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(!s_ready, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->gpio), ESP_ERR_INVALID_ARG, TAG,
                        "GPIO %d cannot be used as output", config->gpio);
    ESP_RETURN_ON_FALSE(config->initial_angle <= SERVO_MAX_ANGLE, ESP_ERR_INVALID_ARG, TAG,
                        "initial angle %u deg out of range", (unsigned)config->initial_angle);

    const ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "LEDC timer config failed");

    /* The initial duty is applied right here in the channel config, so the pin
     * emits a valid frame from the moment it is enabled — never an undefined level. */
    const ledc_channel_config_t channel_config = {
        .gpio_num = config->gpio,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = pulse_to_duty(angle_to_pulse_us(config->initial_angle)),
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "LEDC channel config failed");

    s_ready = true;
    return ESP_OK;
}

esp_err_t servo_set_angle(uint8_t angle_deg)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(angle_deg <= SERVO_MAX_ANGLE, ESP_ERR_INVALID_ARG, TAG,
                        "angle %u deg out of range", (unsigned)angle_deg);

    ESP_RETURN_ON_ERROR(drive_duty(pulse_to_duty(angle_to_pulse_us(angle_deg))), TAG,
                        "cannot drive servo");

    return ESP_OK;
}

esp_err_t servo_rest(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_ERROR(ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, 0), TAG, "stop failed");
    return ESP_OK;
}
