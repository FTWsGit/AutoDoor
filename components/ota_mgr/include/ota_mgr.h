#pragma once

#include <stdint.h>

#include "esp_err.h"

#define OTA_MGR_URI_MAX_LEN 255

typedef enum {
    OTA_MGR_STATE_IDLE,    /* nothing has been started since boot */
    OTA_MGR_STATE_RUNNING, /* downloading / writing the new image */
    OTA_MGR_STATE_SUCCESS, /* new image installed, reboot imminent */
    OTA_MGR_STATE_FAILED,  /* last attempt failed, see last_error */
} ota_mgr_state_t;

typedef struct {
    ota_mgr_state_t state;
    esp_err_t last_error;
    uint8_t progress_pct; /* 0..100, only meaningful while RUNNING */
} ota_mgr_status_t;

/**
 * @brief Start an over-the-air update from an HTTPS URL. Returns immediately.
 *
 * The download runs in its own task; poll ota_mgr_get_status() for the result. On success
 * the device reboots into the new image (which must then be confirmed by the `health`
 * component, otherwise the bootloader rolls back).
 *
 * @return ESP_OK (started), ESP_ERR_INVALID_ARG (NULL, too long, not https://),
 *         ESP_ERR_INVALID_STATE (an update is already running), ESP_ERR_NO_MEM.
 */
esp_err_t ota_mgr_start(const char *uri);

void ota_mgr_get_status(ota_mgr_status_t *status);

const char *ota_mgr_state_name(ota_mgr_state_t state);
