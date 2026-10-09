#include "mqtt_mgr.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"

#include "secrets.h"

static const char *TAG = "mqtt_mgr";

static esp_mqtt_client_handle_t s_client;
static mqtt_mgr_message_cb_t s_on_message;
static void *s_cb_ctx;
static volatile bool s_connected;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_client_started;

/* ---- client lifecycle ------------------------------------------------------------------- */

static bool sta_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    return netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
           ip_info.ip.addr != 0;
}

/* The client reconnects on its own after the first start, so it only has to be started once. */
static void start_client_once(void)
{
    bool must_start = false;

    portENTER_CRITICAL(&s_lock);
    if (!s_client_started) {
        s_client_started = true;
        must_start = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (!must_start) {
        return;
    }

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start client: %s (will retry on next GOT_IP)", esp_err_to_name(err));
        portENTER_CRITICAL(&s_lock);
        s_client_started = false;
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    ESP_LOGI(TAG, "client started");
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    start_client_once();
}

/* ---- MQTT events ------------------------------------------------------------------------ */

static void log_if_nonzero(const char *what, int code)
{
    if (code != 0) {
        ESP_LOGE(TAG, "%s: 0x%x", what, code);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "connected, subscribing to \"%s\"", CONFIG_AUTODOOR_MQTT_TOPIC);
        s_connected = true;
        if (esp_mqtt_client_subscribe(event->client, CONFIG_AUTODOOR_MQTT_TOPIC, 0) < 0) {
            ESP_LOGE(TAG, "subscribe request failed");
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
            ESP_LOGW(TAG, "dropping fragmented message (%d of %d bytes)", event->data_len,
                     event->total_data_len);
            break;
        }
        ESP_LOGI(TAG, "message on \"%.*s\" (%d bytes)", event->topic_len, event->topic,
                 event->data_len);
        s_on_message(event->data, (size_t)event->data_len, s_cb_ctx);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "client error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_if_nonzero("esp-tls error", event->error_handle->esp_tls_last_esp_err);
            log_if_nonzero("tls stack error", event->error_handle->esp_tls_stack_err);
            log_if_nonzero("socket errno", event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        ESP_LOGD(TAG, "event id %d", (int)id);
        break;
    }
}

/* ---- public API ------------------------------------------------------------------------- */

esp_err_t mqtt_mgr_start(const mqtt_mgr_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL && config->on_message != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "on_message callback is required");
    ESP_RETURN_ON_FALSE(s_client == NULL, ESP_ERR_INVALID_STATE, TAG, "already started");

    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = ESP_MQTT_BROKER,
        .credentials.client_id = ESP_MQTT_CLIENT_ID,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
    ESP_RETURN_ON_FALSE(client != NULL, ESP_FAIL, TAG, "esp_mqtt_client_init failed");

    esp_err_t err =
        esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot register MQTT handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client);
        return err;
    }

    s_on_message = config->on_message;
    s_cb_ctx = config->ctx;
    s_client = client;

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot register GOT_IP handler: %s", esp_err_to_name(err));
        s_client = NULL;
        esp_mqtt_client_destroy(client);
        return err;
    }

    if (sta_has_ip()) { /* network came up before we were called */
        start_client_once();
    }
    return ESP_OK;
}

bool mqtt_mgr_is_connected(void)
{
    return s_connected;
}
