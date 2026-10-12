#include "ota_mgr.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OTA_TASK_STACK   8192 /* TLS needs far more than the 4 KB httpd handler stack */
#define OTA_TASK_PRIO    5
#define OTA_REBOOT_DELAY pdMS_TO_TICKS(1000)
#define OTA_URI_SCHEME   "https://"

extern const char ca_crt_start[] asm("_binary_ca_crt_start");

static const char *TAG = "ota_mgr";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ota_mgr_status_t s_status = {.state = OTA_MGR_STATE_IDLE, .last_error = ESP_OK};

/* ---- status helpers --------------------------------------------------------------------- */

static void set_status(ota_mgr_state_t state, esp_err_t err, uint8_t progress)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.last_error = err;
    s_status.progress_pct = progress;
    portEXIT_CRITICAL(&s_lock);
}

static void set_progress(uint8_t progress)
{
    portENTER_CRITICAL(&s_lock);
    s_status.progress_pct = progress;
    portEXIT_CRITICAL(&s_lock);
}

void ota_mgr_get_status(ota_mgr_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

const char *ota_mgr_state_name(ota_mgr_state_t state)
{
    switch (state) {
    case OTA_MGR_STATE_IDLE:
        return "idle";
    case OTA_MGR_STATE_RUNNING:
        return "running";
    case OTA_MGR_STATE_SUCCESS:
        return "success";
    case OTA_MGR_STATE_FAILED:
        return "failed";
    }
    return "unknown";
}

/* ---- download / install ----------------------------------------------------------------- */

static void log_new_image_version(esp_https_ota_handle_t handle)
{
    esp_app_desc_t new_desc;
    if (esp_https_ota_get_img_desc(handle, &new_desc) == ESP_OK) {
        ESP_LOGI(TAG, "new image: version \"%s\" (running: \"%s\")", new_desc.version,
                 esp_app_get_description()->version);
    }
}

static void update_progress(esp_https_ota_handle_t handle)
{
    int total = esp_https_ota_get_image_size(handle);
    int done = esp_https_ota_get_image_len_read(handle);
    if (total > 0 && done >= 0) {
        set_progress((uint8_t)((int64_t)done * 100 / total));
    }
}

static esp_err_t download_and_install(const char *uri)
{
    const esp_http_client_config_t http_config = {
        .url = uri,
        .cert_pem = ca_crt_start,
        .timeout_ms = CONFIG_AUTODOOR_OTA_TIMEOUT_MS,
        .keep_alive_enable = true,
    };
    const esp_https_ota_config_t ota_config = {.http_config = &http_config};

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start download: %s", esp_err_to_name(err));
        return err;
    }
    log_new_image_version(handle);

    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        update_progress(handle);
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "connection closed before the whole image was received");
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err;
    }

    /* Validates the image and switches the boot partition. */
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(err));
    }
    return err;
}

static void ota_task(void *arg)
{
    char *uri = arg;
    ESP_LOGI(TAG, "starting update from %s", uri);

    esp_err_t err = download_and_install(uri);
    free(uri);

    if (err == ESP_OK) {
        set_status(OTA_MGR_STATE_SUCCESS, ESP_OK, 100);
        ESP_LOGI(TAG, "update installed, rebooting");
        vTaskDelay(OTA_REBOOT_DELAY);
        esp_restart();
    }

    set_status(OTA_MGR_STATE_FAILED, err, 0);
    vTaskDelete(NULL);
}

esp_err_t ota_mgr_start(const char *uri)
{
    ESP_RETURN_ON_FALSE(uri != NULL, ESP_ERR_INVALID_ARG, TAG, "uri is NULL");
    size_t len = strlen(uri);
    ESP_RETURN_ON_FALSE(len > strlen(OTA_URI_SCHEME) && len <= OTA_MGR_URI_MAX_LEN &&
                            strncmp(uri, OTA_URI_SCHEME, strlen(OTA_URI_SCHEME)) == 0,
                        ESP_ERR_INVALID_ARG, TAG, "uri must be an https:// URL of <= %d chars",
                        OTA_MGR_URI_MAX_LEN);

    char *uri_copy = strdup(uri);
    ESP_RETURN_ON_FALSE(uri_copy != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    bool busy;
    portENTER_CRITICAL(&s_lock);
    busy = (s_status.state == OTA_MGR_STATE_RUNNING || s_status.state == OTA_MGR_STATE_SUCCESS);
    if (!busy) {
        s_status.state = OTA_MGR_STATE_RUNNING;
        s_status.last_error = ESP_OK;
        s_status.progress_pct = 0;
    }
    portEXIT_CRITICAL(&s_lock);

    if (busy) {
        free(uri_copy);
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(ota_task, "ota", OTA_TASK_STACK, uri_copy, OTA_TASK_PRIO, NULL) != pdPASS) {
        free(uri_copy);
        set_status(OTA_MGR_STATE_FAILED, ESP_ERR_NO_MEM, 0);
        ESP_LOGE(TAG, "cannot create OTA task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
