#include "health.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HEALTH_TASK_STACK  3072
#define HEALTH_TASK_PRIO   4
#define HEALTH_POLL_PERIOD pdMS_TO_TICKS(500)

static const char *TAG = "health";

static const health_check_t *s_checks;
static size_t s_count;
static bool s_started;

/** @return number of failing checks (names are logged when `log_failures` is set). */
static size_t count_failures(bool log_failures)
{
    size_t failures = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (!s_checks[i].check()) {
            failures++;
            if (log_failures) {
                ESP_LOGE(TAG, "check \"%s\" failed", s_checks[i].name);
            }
        }
    }
    return failures;
}

static void health_task(void *arg)
{
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(CONFIG_AUTODOOR_HEALTH_TIMEOUT_S * 1000);

    while (count_failures(false) != 0 && xTaskGetTickCount() < deadline) {
        vTaskDelay(HEALTH_POLL_PERIOD);
    }

    if (count_failures(true) == 0) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "all checks passed, new firmware confirmed");
        } else {
            ESP_LOGE(TAG, "cannot confirm firmware: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "new firmware is unhealthy, rolling back");
        /* Only returns on failure, e.g. when there is no previous image to go back to. */
        esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG, "rollback failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

esp_err_t health_start(const health_check_t *checks, size_t count)
{
    ESP_RETURN_ON_FALSE(checks != NULL && count > 0, ESP_ERR_INVALID_ARG, TAG, "no checks given");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "already started");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "firmware is not pending verification, nothing to do");
        return ESP_OK;
    }

    s_checks = checks;
    s_count = count;

    ESP_LOGW(TAG, "new firmware pending verification, %d s to prove it works",
             CONFIG_AUTODOOR_HEALTH_TIMEOUT_S);
    BaseType_t ok =
        xTaskCreate(health_task, "health", HEALTH_TASK_STACK, NULL, HEALTH_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "cannot create health task");
    s_started = true;
    return ESP_OK;
}
