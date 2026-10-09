/*
 * AutoDoor - composition root.
 *
 * This file only wires components together; the logic lives in the components:
 *   door      servo-driven door (open / hold / close)
 *   wifi_mgr  configuration AP + station with automatic reconnect
 *   mqtt_mgr  receives commands from the broker
 *   web_ui    configuration page (Wi-Fi credentials, OTA)
 *   ota_mgr   over-the-air firmware update
 *   health    confirms / rolls back a freshly updated firmware
 *
 * Error policy: only failures that make everything else pointless (NVS, TCP/IP stack,
 * default event loop) abort at boot. Everything after that degrades gracefully, so the
 * device stays reachable for configuration and OTA even if one subsystem is broken.
 */
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "door.h"
#include "health.h"
#include "mqtt_mgr.h"
#include "web_ui.h"
#include "wifi_mgr.h"

static const char *TAG = "app";

static void on_mqtt_message(const char *data, size_t len, void *ctx)
{
    const char *open_cmd = CONFIG_AUTODOOR_CMD_OPEN;

    if (len == strlen(open_cmd) && memcmp(data, open_cmd, len) == 0) {
        esp_err_t err = door_open();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "door_open failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "ignoring unknown command (%u bytes)", (unsigned)len);
    }
}

static void log_if_failed(const char *what, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s - continuing without it", what, esp_err_to_name(err));
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    /* Boot-critical: nothing works without these. */
    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Everything below degrades instead of aborting. */
    log_if_failed("door init", door_init());

    const mqtt_mgr_config_t mqtt_config = {.on_message = on_mqtt_message};
    log_if_failed("MQTT start", mqtt_mgr_start(&mqtt_config));
    log_if_failed("Wi-Fi start", wifi_mgr_start());
    log_if_failed("web UI start", web_ui_start());

    /* After an OTA update: confirm the new firmware only if it can do its job, so a broken
     * build rolls back by itself. Every check must stay satisfiable by a healthy device. */
    static const health_check_t checks[] = {
        {"door", door_is_ready},
        {"wifi", wifi_mgr_has_ip},
        {"mqtt", mqtt_mgr_is_connected},
        {"web", web_ui_is_running},
    };
    log_if_failed("health check", health_start(checks, sizeof(checks) / sizeof(checks[0])));
}
