#include "mqtt_client.h"
#include "esp_log.h"
#include "string.h"

#include "mqtt_mgr.h"
#include "servo.h"
#include "secrets.h"


const char* TAG = "MQTT";
static void log_error_if_nonzero(const char *message, int error_code)
{
	if (error_code != 0) ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
}

static void mqtt_handler(void* event_handler_arg, esp_event_base_t base, int32_t event_id, void* event_data) {

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
	esp_mqtt_event_handle_t event = event_data;
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	switch ((esp_mqtt_event_id_t)event_id)
	{
	// MQTT连接成功
	case MQTT_EVENT_CONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		// 订阅消息
		msg_id = esp_mqtt_client_subscribe(client, "dormdoor006", 0);
		break;
	// MQTT连接断开
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		break;
	// MQTT订阅成功
	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	// MQTT取消订阅成功
	case MQTT_EVENT_UNSUBSCRIBED:
		ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		break;
	// MQTT发布成功
	case MQTT_EVENT_PUBLISHED:
		ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		break;
	// MQTT收到数据
	case MQTT_EVENT_DATA:
		ESP_LOGI(TAG, "MQTT_EVENT_DATA");
		printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
		printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (event->data_len == 6 && strncmp(event->data, "openup", 6) == 0) {
            printf("Start opening\r\n");

			servo_switch(SERVO_RIGHT, 250);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
			
			servo_switch(SERVO_STOP, 0);
            vTaskDelay(2000 / portTICK_PERIOD_MS);

			servo_switch(SERVO_LEFT, 300);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
			
			servo_switch(SERVO_STOP, 0);

            printf("Stop opening\r\n");
        }
		break;
	// MQTT错误
	case MQTT_EVENT_ERROR:
		ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
		{
			log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
			log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
			log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
			ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
		}
		break;
	default:
		ESP_LOGI(TAG, "Other event id:%d", event->event_id);
		break;
	}
}

void mqtt_start() {
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = ESP_MQTT_BROKER,
        .credentials.client_id = ESP_MQTT_CLIENT_ID
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_handler, NULL);

    esp_mqtt_client_start(client);
}

