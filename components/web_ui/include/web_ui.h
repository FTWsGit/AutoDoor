#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Start the configuration web server (port 80).
 *
 * Routes: GET /, GET /info, POST /wifi_sta, POST /ota, GET /ota/status.
 * The network stack must be initialised (esp_netif_init) before calling.
 */
esp_err_t web_ui_start(void);

/** @return true while the server is running. */
bool web_ui_is_running(void);
