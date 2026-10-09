#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/**
 * Called for every complete message received on the configured topic.
 *
 * @param data  payload, NOT NUL-terminated
 * @param len   payload length in bytes
 * @param ctx   the pointer given in mqtt_mgr_config_t
 *
 * Runs on the MQTT task: keep it short and never block.
 */
typedef void (*mqtt_mgr_message_cb_t)(const char *data, size_t len, void *ctx);

typedef struct {
    mqtt_mgr_message_cb_t on_message; /* required */
    void *ctx;
} mqtt_mgr_config_t;

/**
 * @brief Prepare the MQTT client; it connects automatically once the station has an IP.
 *
 * Works regardless of whether the network is already up or comes up later.
 * Broker, client id and topic come from secrets.h / Kconfig.
 */
esp_err_t mqtt_mgr_start(const mqtt_mgr_config_t *config);

/** @return true while the client is connected to the broker. */
bool mqtt_mgr_is_connected(void);
