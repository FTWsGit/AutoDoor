#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "wifi_mgr.h"
#include "mqtt_mgr.h"
#include "servo.h"
#include "http_server.h"

static const char *TAG = "APP_MAIN";

static void app_network_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    static bool mqtt_started = false;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (mqtt_started) {
            return;
        }
        mqtt_started = true;

        TaskHandle_t *servo = (TaskHandle_t *)arg;
        ESP_LOGI(TAG, "STA got IP, starting MQTT");
        mqtt_start(*servo);
    }
}

void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    static TaskHandle_t xServoHandle = NULL;
    servo_init(&xServoHandle);

    wifi_start();   // 内部 esp_netif_init() 初始化 lwIP，必须先于 httpd

    http_server_start();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, app_network_handler, &xServoHandle, NULL));
}
