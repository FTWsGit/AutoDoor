#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "wifi_mgr.h"
#include "mqtt_mgr.h"
#include "servo.h"
#include "http_server.h"

void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    TaskHandle_t xServoHandle = NULL;
    servo_init(&xServoHandle);

    wifi_start();

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    mqtt_start(xServoHandle);
    http_server_start();
}
