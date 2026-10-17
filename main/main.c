#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

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

/* Automatic light sleep: the CPU clocks down and sleeps whenever nothing needs it, and Wi-Fi
 * modem sleep (see wifi_mgr) keeps the radio off between beacons. */
static void power_init(void)
{
#if CONFIG_PM_ENABLE
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_AUTODOOR_PM_MAX_FREQ_MHZ,
        .min_freq_mhz = CONFIG_AUTODOOR_PM_MIN_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#endif
    };
    log_if_failed("power management", esp_pm_configure(&pm_config));
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE is off: no power management");
#endif
}

void app_main(void)
{
    /* Boot-critical: nothing works without these. */
    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    power_init();

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
