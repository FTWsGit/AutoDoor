#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** A named probe that returns true when one subsystem is working. */
typedef struct {
    const char *name;
    bool (*check)(void);
} health_check_t;

/**
 * @brief Confirm (or roll back) a freshly OTA-installed firmware.
 *
 * After an OTA update the bootloader starts the new image in the "pending verify" state
 * and reverts to the previous image on the next reset unless the app confirms itself.
 * This function does nothing on ordinary boots. When the image is pending verification it
 * polls all `checks`:
 *   - all pass within CONFIG_AUTODOOR_HEALTH_TIMEOUT_S -> image is marked valid;
 *   - otherwise                                        -> rollback to the previous image
 *                                                         and reboot.
 * (A crash or watchdog reset before confirmation also triggers a rollback.)
 *
 * @param checks  array with static storage duration (it is used by a background task)
 * @param count   number of entries
 */
esp_err_t health_start(const health_check_t *checks, size_t count);
