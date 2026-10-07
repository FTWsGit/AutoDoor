#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <string.h>

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
            esp_err_t err = esp_wifi_connect();
			if (err != ESP_OK) {
                ESP_LOGW("WIFI_EVENT", "WIFI connect failed: %s", esp_err_to_name(err));
            }
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

    /* WIFI AP Callback */
    if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
		ip_event_assigned_ip_to_client_t* event_info = (ip_event_assigned_ip_to_client_t *)event_data;
		ESP_LOGI("WIFI_AP", "Device connected MAC: " MACSTR, MAC2STR(event_info->mac));
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t* event_info = (wifi_event_ap_stadisconnected_t*)event_data;
		ESP_LOGI("WIFI_AP", "Device disconnected MAC: " MACSTR, MAC2STR(event_info->mac));
	}
}

void wifi_start() {
    ESP_ERROR_CHECK(esp_netif_init());

    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, wifi_handler, NULL, NULL);

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

    nvs_handle_t nvs_handle;
    esp_err_t err;
    bool enable_sta = true;
    err = nvs_open("wifi", NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 命名空间不存在 = 首次启动，还没有保存的 WiFi 配置，走 AP 配网流程
        ESP_LOGW("WIFI", "No saved wifi config in nvs, AP mode only");
        enable_sta = false;
    } else if (err != ESP_OK) {
        ESP_LOGW("WIFI", "Failed to open nvs: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    char wifi_ssid[33] = {0};
    char wifi_passwd[65] = {0};
    size_t ssid_length = sizeof(wifi_ssid);
    size_t password_length = sizeof(wifi_passwd);

    err = nvs_get_str(nvs_handle, "ssid", wifi_ssid, &ssid_length);
    if (err != ESP_OK) {
        ESP_LOGW("WIFI", "Failed to get wifi SSID: %s", esp_err_to_name(err));
        enable_sta = false;
    }
    err = nvs_get_str(nvs_handle, "password", wifi_passwd, &password_length);
    if (err != ESP_OK) {
        ESP_LOGW("WIFI", "Failed to get wifi password: %s", esp_err_to_name(err));
        enable_sta = false;
    }
    nvs_close(nvs_handle);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_DEFAULT_SSID,
            .password = WIFI_AP_DEFAULT_PASSWD,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA2_PSK
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    if (enable_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_LOGI("WIFI", "Set WIFI to AP-STA Mode");
    }
    else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_LOGI("WIFI", "Set WIFI to AP-Only Mode");
    }

    if (enable_sta) {
        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, wifi_ssid,
            strnlen(wifi_ssid, sizeof(sta_config.sta.ssid)));
        memcpy(sta_config.sta.password, wifi_passwd,
            strnlen(wifi_passwd, sizeof(sta_config.sta.password)));
        sta_config.sta.bssid_set = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }

	ESP_ERROR_CHECK(esp_wifi_start());
}
