#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/**
 * Servo position, expressed in degrees.
 *
 * Angle is the servo's own semantic. The direction is reversed relative to the usual
 * convention: 0 deg maps to the longest pulse and SERVO_MAX_ANGLE deg to the shortest,
 * linearly (see servo.c for the pulse limits). Callers never deal with pulse widths.
 */
#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180

typedef struct {
    gpio_num_t gpio;        /* PWM output pin */
    uint8_t initial_angle;  /* angle applied as soon as the output is enabled */
} servo_config_t;

/**
 * How the servo should hold the target position once reached.
 * The caller expresses the intent (which position needs sustained torque);
 * the servo driver decides how that is realised (e.g. stopping the PWM output).
 */
typedef enum {
    SERVO_HOLD_KEEP = 0,      /* keep driving the servo until the next command */
    SERVO_HOLD_RELEASE_LATER, /* keep driving until settled, then de-energise */
} servo_hold_t;

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
 * @brief Move the servo to the given angle, with the requested holding behaviour.
 *
 * @param angle_deg target angle, 0..SERVO_MAX_ANGLE
 * @param hold whether to keep torque after settling or de-energise
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE (not initialised), ESP_ERR_INVALID_ARG (out of range),
 *         otherwise the error reported by the LEDC or esp_timer driver.
 */
esp_err_t servo_set_angle(uint8_t angle_deg, servo_hold_t hold);
