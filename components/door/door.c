#include "door.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "servo.h"

#define DOOR_TASK_STACK 3072
#define DOOR_TASK_PRIO  3

static const char *TAG = "door";
static TaskHandle_t s_task;

static void move_to(uint32_t pulse_us, const char *what)
{
    esp_err_t err = servo_set_pulse_us(pulse_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to move to %s position: %s", what, esp_err_to_name(err));
    }
}

static void door_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "opening");
        move_to(CONFIG_AUTODOOR_DOOR_OPEN_US, "open");
        vTaskDelay(pdMS_TO_TICKS(CONFIG_AUTODOOR_DOOR_OPEN_HOLD_MS));
        move_to(CONFIG_AUTODOOR_DOOR_CLOSED_US, "closed");
        ESP_LOGI(TAG, "closed");
    }
}

esp_err_t door_init(void)
{
    ESP_RETURN_ON_FALSE(s_task == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    const servo_config_t servo_config = {
        .gpio = CONFIG_AUTODOOR_SERVO_GPIO,
        .initial_pulse_us = CONFIG_AUTODOOR_DOOR_CLOSED_US,
    };
    ESP_RETURN_ON_ERROR(servo_init(&servo_config), TAG, "servo init failed");

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate(door_task, "door", DOOR_TASK_STACK, NULL, DOOR_TASK_PRIO, &task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "cannot create door task");

    s_task = task;
    return ESP_OK;
}

bool door_is_ready(void)
{
    return s_task != NULL;
}

esp_err_t door_open(void)
{
    ESP_RETURN_ON_FALSE(s_task != NULL, ESP_ERR_INVALID_STATE, TAG, "door not initialised");
    xTaskNotifyGive(s_task);
    return ESP_OK;
}
