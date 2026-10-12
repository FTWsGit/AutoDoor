#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define WIFI_MGR_SSID_MAX_LEN     32
#define WIFI_MGR_PASSWORD_MAX_LEN 64

/**
 * @brief Bring up Wi-Fi.
 *
 * - Always opens the configuration access point.
 * - If credentials are stored in NVS, also connects as a station and keeps reconnecting
 *   with exponential backoff (1 s ... 30 s) for as long as the link is down.
 *
 * Requires the default event loop. Missing/unreadable credentials are not an error: the
 * device simply stays in AP-only mode. On failure the Wi-Fi stack may be partially set up,
 * so the caller should treat Wi-Fi as unavailable rather than retry.
 */
esp_err_t wifi_mgr_start(void);

/**
 * @brief Persist station credentials; they are used on the next boot.
 *
 * @param ssid      1..32 characters
 * @param password  empty (open network) or 8..64 characters
 * @return ESP_OK, ESP_ERR_INVALID_ARG (validation failed) or an NVS error.
 */
esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password);

/** @return true while the station interface holds an IP address. */
bool wifi_mgr_has_ip(void);
