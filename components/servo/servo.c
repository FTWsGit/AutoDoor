#include "servo.h"

#include <stdbool.h>

#include "driver/ledc.h"
#include "esp_check.h"
#include "sdkconfig.h"

#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

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

/* The LEDC timer runs from APB_CLK (LEDC_AUTO_CLK picks it first on the C3), and APB_CLK is
 * neither DFS- nor light-sleep-compatible: DFS can drop it from 80 MHz and light sleep gates it,
 * which stretches/truncates the 50 Hz frames. So while the servo is being driven we hold an
 * APB_FREQ_MAX lock (this also keeps the chip out of light sleep). servo_rest() releases it. */
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock;
static bool s_pm_held;

static esp_err_t pm_hold(void)
{
    if (s_pm_lock == NULL || s_pm_held) {
        return ESP_OK;
    }
    esp_err_t err = esp_pm_lock_acquire(s_pm_lock);
    if (err == ESP_OK) {
        s_pm_held = true;
    }
    return err;
}

static void pm_release(void)
{
    if (s_pm_lock != NULL && s_pm_held) {
        esp_pm_lock_release(s_pm_lock);
        s_pm_held = false;
    }
}
#else
static inline esp_err_t pm_hold(void) { return ESP_OK; }
static inline void pm_release(void) {}
#endif

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
    if (s_ready) { /* idempotent: a retry after a partial failure is fine */
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(config->gpio), ESP_ERR_INVALID_ARG, TAG,
                        "GPIO %d cannot be used as output", config->gpio);
    ESP_RETURN_ON_FALSE(config->initial_angle <= SERVO_MAX_ANGLE, ESP_ERR_INVALID_ARG, TAG,
                        "initial angle %u deg out of range", (unsigned)config->initial_angle);

    esp_err_t ret = ESP_OK;
#if CONFIG_PM_ENABLE
    ESP_GOTO_ON_ERROR(esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "servo", &s_pm_lock), err, TAG,
                      "PM lock create failed");
    ESP_GOTO_ON_ERROR(pm_hold(), err, TAG, "PM lock acquire failed");
#endif

    const ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_GOTO_ON_ERROR(ledc_timer_config(&timer_config), err, TAG, "LEDC timer config failed");

    /* The initial duty is applied right here in the channel config, so the pin
     * emits a valid frame from the moment it is enabled — never an undefined level. */
    const ledc_channel_config_t channel_config = {
        .gpio_num = config->gpio,
        .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = pulse_to_duty(angle_to_pulse_us(config->initial_angle)),
    };
    ESP_GOTO_ON_ERROR(ledc_channel_config(&channel_config), err, TAG, "LEDC channel config failed");

    s_ready = true;
    return ESP_OK;

err:
#if CONFIG_PM_ENABLE
    /* Roll the PM lock back so a failed init can't leave APB_FREQ_MAX held forever
     * (and a retry doesn't leak a second lock over s_pm_lock). */
    pm_release();
    if (s_pm_lock != NULL) {
        esp_pm_lock_delete(s_pm_lock);
        s_pm_lock = NULL;
    }
#endif
    return ret;
}

esp_err_t servo_set_angle(uint8_t angle_deg)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_FALSE(angle_deg <= SERVO_MAX_ANGLE, ESP_ERR_INVALID_ARG, TAG,
                        "angle %u deg out of range", (unsigned)angle_deg);

    /* hold APB freq (and keep light sleep out) before the PWM output is (re)enabled */
    ESP_RETURN_ON_ERROR(pm_hold(), TAG, "cannot hold PM lock");
    ESP_RETURN_ON_ERROR(drive_duty(pulse_to_duty(angle_to_pulse_us(angle_deg))), TAG,
                        "cannot drive servo");

    return ESP_OK;
}

esp_err_t servo_rest(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ESP_RETURN_ON_ERROR(ledc_stop(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, 0), TAG, "stop failed");
    pm_release(); /* output is off, DFS / light sleep may resume */
    return ESP_OK;
}
