#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/* Pulse widths outside this range are rejected to protect the servo mechanics. */
#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2500

typedef struct {
    gpio_num_t gpio;           /* PWM output pin */
    uint32_t initial_pulse_us; /* pulse emitted as soon as the output is enabled */
} servo_config_t;

/**
 * @brief Configure LEDC (50 Hz PWM) for a single hobby servo.
 *
 * This is a plain hardware driver: it knows nothing about doors or directions.
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG (bad config) or ESP_ERR_INVALID_STATE (already initialised),
 *         otherwise the error reported by the LEDC driver.
 */
esp_err_t servo_init(const servo_config_t *config);

/**
 * @brief Move the servo by setting the PWM pulse width.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE (not initialised) or ESP_ERR_INVALID_ARG (out of range).
 */
esp_err_t servo_set_pulse_us(uint32_t pulse_us);
