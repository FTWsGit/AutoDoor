#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "string.h"

#include "secrets.h"

#define SERVO_STOP_µs 1500
#define SERVO_RIGHT_µs 1750
#define SERVO_LEFT_µs 1200
#define SERVO_ALL_µs 20000


void WIFI_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_START");
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

    static uint8_t connect_count = 0;
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_DISCONNECTED");
		connect_count++;
		if (connect_count < 6)
		{
			ESP_ERROR_CHECK(esp_wifi_connect());
		}
		else{
			ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_DISCONNECTED 6 times");
		}
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI("IP_EVENT", "IP_EVENT_STA_GOT_IP");
        ip_event_got_ip_t* info = (ip_event_got_ip_t*)event_data;
        ESP_LOGI("WIFI_EVENT", "got ip:" IPSTR "", IP2STR(&info->ip_info.ip));
    }
}

void WIFI_start() {
    ESP_ERROR_CHECK(esp_netif_init());

    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, WIFI_handler, NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, WIFI_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, WIFI_handler, NULL, NULL);

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta_config = {
		.sta = {
			.ssid = ESP_WIFI_STA_SSID,
			.password = ESP_WIFI_STA_PASSWD,
			.bssid_set = false,
		},
	};

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

	ESP_ERROR_CHECK(esp_wifi_start());
}

const char* TAG = "MQTT";
static void log_error_if_nonzero(const char *message, int error_code)
{
	if (error_code != 0)
	{
		ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
	}
}

void MQTT_handler(void* event_handler_arg, esp_event_base_t base, int32_t event_id, void* event_data) {

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
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_RIGHT_µs * 8192) / SERVO_ALL_µs);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_STOP_µs * 8192) / SERVO_ALL_µs);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_LEFT_µs * 8192) / SERVO_ALL_µs);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (SERVO_STOP_µs * 8192) / SERVO_ALL_µs);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
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

void MQTT_start() {
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = ESP_MQTT_BROKER,
        .credentials.client_id = ESP_MQTT_CLIENT_ID
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, MQTT_handler, NULL);

    esp_mqtt_client_start(client);
}


void LEDC_init() {
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_OUTPUT);

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
        .duty = (SERVO_STOP_µs * 8192) / SERVO_ALL_µs,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .gpio_num = GPIO_NUM_1, 
        .timer_sel = LEDC_TIMER_0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}


void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    LEDC_init();

    WIFI_start();

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    MQTT_start();
}
