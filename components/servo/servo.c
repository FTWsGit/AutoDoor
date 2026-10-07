#include "freertos/freeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "servo.h"

static void servo_set(servo_direction_t direct) {
    switch (direct) 
    {
    case SERVO_MIDDLE:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_MIDDLE_US * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    case SERVO_LEFT:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, ((SERVO_LEFT_US) * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    case SERVO_RIGHT:
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, ((SERVO_RIGHT_US) * 8192) / SERVO_ALL_US);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        break;
    }
}

static void servoTask(void *pvParameters) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI("SERVO", "Start opening\r\n");

		servo_set(SERVO_RIGHT);

        vTaskDelay(2000 / portTICK_PERIOD_MS);

		servo_set(SERVO_LEFT);

        ESP_LOGI("SERVO", "Closed\r\n");
    }
}

void servo_init(TaskHandle_t* pxServoHandle) {
    gpio_set_direction(SERVO_GPIO_NUM, GPIO_MODE_OUTPUT);

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .clk_cfg = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .timer_num = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = (SERVO_LEFT_US * 8192) / SERVO_ALL_US,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .gpio_num = SERVO_GPIO_NUM, 
        .timer_sel = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    BaseType_t ok = xTaskCreate(servoTask, "servoTask", 2048, NULL, 3, pxServoHandle);
    if (ok != pdPASS) {
        ESP_LOGE("SERVO", "xTaskCreate failed");
    }
}

