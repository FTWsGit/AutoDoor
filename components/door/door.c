#include "door.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "servo.h"

#define DOOR_TASK_STACK 3072
#define DOOR_TASK_PRIO  3
#define DOOR_SETTLE_MS 1000

static const char *TAG = "door";
static TaskHandle_t s_task;

static void move_to(uint8_t angle_deg, const char *what)
{
    esp_err_t err = servo_set_angle(angle_deg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to move to %s position: %s", what, esp_err_to_name(err));
    }
}

static void rest_servo(void)
{
    esp_err_t err = servo_rest();
    if (err != ESP_OK) {
        /* The servo just stays energised until the next move: wasteful, not dangerous. */
        ESP_LOGE(TAG, "failed to release servo: %s", esp_err_to_name(err));
    }
}

#define OPEN_NOTIFY_BIT 0x01

static void door_task(void *arg)
{
    uint32_t bits = 0;
    /* servo_init() has just driven the closed position: give it time to get there, then rest. */
    TickType_t wait = pdMS_TO_TICKS(DOOR_SETTLE_MS);

    for (;;) {
        /* Idle (servo at rest) waits forever; after a move, the wait doubles as the settle time. */
        if (xTaskNotifyWait(0, OPEN_NOTIFY_BIT, &bits, wait) == pdFALSE) {
            rest_servo();
            wait = portMAX_DELAY;
            continue;
        }

        ESP_LOGI(TAG, "opening");
        move_to(CONFIG_AUTODOOR_DOOR_OPEN_DEG, "open");

        // Every open command sets OPEN_NOTIFY_BIT and restarts the holding
        // count down; only when no new command arrives within the hold window
        // do we fall through and close the door.
        while (xTaskNotifyWait(0, OPEN_NOTIFY_BIT, &bits,
                               pdMS_TO_TICKS(CONFIG_AUTODOOR_DOOR_OPEN_HOLD_MS)) == pdTRUE &&
               (bits & OPEN_NOTIFY_BIT)) {
            ESP_LOGI(TAG, "hold restarted by open command");
        }

        move_to(CONFIG_AUTODOOR_DOOR_CLOSED_DEG, "closed");
        ESP_LOGI(TAG, "closed");
        wait = pdMS_TO_TICKS(DOOR_SETTLE_MS);
    }
}

esp_err_t door_init(void)
{
    ESP_RETURN_ON_FALSE(s_task == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

    const servo_config_t servo_config = {
        .gpio = CONFIG_AUTODOOR_SERVO_GPIO,
        .initial_angle = CONFIG_AUTODOOR_DOOR_CLOSED_DEG,
    };
    ESP_RETURN_ON_ERROR(servo_init(&servo_config), TAG, "servo init failed");

    /* servo_init() already drives the closed position. Arm the release too, otherwise the
     * servo stays energised until the first open/close cycle. */
    move_to(CONFIG_AUTODOOR_DOOR_CLOSED_DEG, "closed");

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
    xTaskNotify(s_task, OPEN_NOTIFY_BIT, eSetBits);
    return ESP_OK;
}
