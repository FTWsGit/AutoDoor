#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialise the door actuator and move it to the closed position.
 *
 * Creates the task that performs open/close cycles. Safe to call once.
 */
esp_err_t door_init(void);

/** @return true once door_init() succeeded and the door can be opened. */
bool door_is_ready(void);

/**
 * @brief Request one open -> hold -> close cycle. Returns immediately.
 *
 * Callable from any task (not from an ISR). Requests are coalesced, not queued: a request
 * made while the door is open restarts the hold countdown, so one cycle covers them all.
 *
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if the door is not initialised.
 */
esp_err_t door_open(void);
