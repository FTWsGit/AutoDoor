#include "esp_wifi.h"
#include "esp_log.h"
#include "wifi_mgr.h"
#include "secrets.h"

static void wifi_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    
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

void wifi_start() {
    ESP_ERROR_CHECK(esp_netif_init());

    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL);

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
