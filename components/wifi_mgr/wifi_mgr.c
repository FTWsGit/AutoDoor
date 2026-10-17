#include "wifi_mgr.h"

#include <string.h>
#include <sys/param.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#define NVS_NAMESPACE    "wifi"
#define NVS_KEY_SSID     "ssid"
#define NVS_KEY_PASSWORD "password"

#define RECONNECT_BASE_DELAY_MS 1000
#define RECONNECT_MAX_DELAY_MS  300000
/* Frequent STA scans while the router is down disrupt the config AP (single radio, one
 * channel), so back off slowly: 1 s << 9 = 512 s, capped to RECONNECT_MAX_DELAY_MS. */
#define RECONNECT_MAX_SHIFT     9

/* Delay before retrying a failed switch back to AP+STA. */
#define AP_MODE_RETRY_DELAY_US  (5 * 1000000ULL)

/* The config AP keeps the radio awake (it must beacon every ~100 ms), so it is only kept up while
 * it is needed: right after boot, and whenever the station has no IP. */
#define AP_KEEP_AFTER_IP_US     ((uint64_t)CONFIG_AUTODOOR_AP_KEEP_AFTER_IP_S * 1000000ULL)
#define AP_RETURN_AFTER_LOSS_US ((uint64_t)CONFIG_AUTODOOR_AP_RETURN_AFTER_LOSS_S * 1000000ULL)

static const char *TAG = "wifi_mgr";

static bool s_started;
static volatile bool s_has_ip;
static volatile uint8_t s_retry_count; /* only used for backoff, benign if racy */
static esp_timer_handle_t s_reconnect_timer;
static bool s_sta_enabled; /* credentials are saved, so the station is in use */
static esp_timer_handle_t s_ap_off_timer;
static esp_timer_handle_t s_ap_on_timer;

/* ---- sntp ------------------------------------------------------------------------------- */
static void start_sntp_once(void)
{
    static bool s_sntp_started = false;
    if (s_sntp_started) return;

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_AUTODOOR_SNTP_URL);

    if (esp_netif_sntp_init(&sntp_config) == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "sntp started");
    } else {
        ESP_LOGW(TAG, "esp_netif_sntp_init failed");
    }
}

/* ---- credentials ------------------------------------------------------------------------ */

/**
 * @return ESP_OK, ESP_ERR_NOT_FOUND if nothing usable is stored (first boot), or an NVS error.
 */
static esp_err_t load_credentials(char *ssid, size_t ssid_size, char *password,
                                  size_t password_size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND; /* namespace not created yet */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    size_t len = ssid_size;
    err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &len);
    if (err == ESP_OK) {
        len = password_size;
        err = nvs_get_str(nvs, NVS_KEY_PASSWORD, password, &len);
    }
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && ssid[0] == '\0')) {
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

esp_err_t wifi_mgr_set_credentials(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && password != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL arg");

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    ESP_RETURN_ON_FALSE(ssid_len >= 1 && ssid_len <= WIFI_MGR_SSID_MAX_LEN, ESP_ERR_INVALID_ARG,
                        TAG, "SSID must be 1..%d characters", WIFI_MGR_SSID_MAX_LEN);
    ESP_RETURN_ON_FALSE(password_len == 0 ||
                            (password_len >= 8 && password_len <= WIFI_MGR_PASSWORD_MAX_LEN),
                        ESP_ERR_INVALID_ARG, TAG, "password must be empty or 8..%d characters",
                        WIFI_MGR_PASSWORD_MAX_LEN);

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, NVS_KEY_PASSWORD, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_RETURN_ON_ERROR(err, TAG, "failed to store credentials");
    ESP_LOGI(TAG, "stored credentials for SSID \"%s\"", ssid);
    return ESP_OK;
}

/* ---- reconnect policy ------------------------------------------------------------------- */

static void schedule_reconnect(void)
{
    uint32_t shift = MIN((uint32_t)s_retry_count, (uint32_t)RECONNECT_MAX_SHIFT);
    uint32_t delay_ms =
        MIN((uint32_t)RECONNECT_BASE_DELAY_MS << shift, (uint32_t)RECONNECT_MAX_DELAY_MS);
    if (s_retry_count < UINT8_MAX) {
        s_retry_count++;
    }

    ESP_LOGI(TAG, "reconnecting in %u ms (attempt %u)", (unsigned)delay_ms, s_retry_count);
    esp_timer_stop(s_reconnect_timer); /* fails harmlessly if the timer is not running */
    esp_err_t err = esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot arm reconnect timer: %s", esp_err_to_name(err));
    }
}

static void reconnect_timer_cb(void *arg)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        /* No DISCONNECTED event will follow a failed call, so retry from here. */
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        schedule_reconnect();
    }
}

/* ---- config AP on/off policy ------------------------------------------------------------ */

static void ap_off_timer_cb(void *arg)
{
    if (!s_has_ip) {
        return;
    }

    wifi_sta_list_t clients;
    esp_err_t err = esp_wifi_ap_get_sta_list(&clients);
    if (err == ESP_ERR_WIFI_MODE) {
        return; /* AP is already off (e.g. re-connected after it was switched off) */
    }
    if (err != ESP_OK || clients.num > 0) {
        /* unknown or busy: check again later rather than risk dropping a user */
        esp_timer_start_once(s_ap_off_timer, AP_KEEP_AFTER_IP_US);
        return;
    }

    ESP_LOGI(TAG, "station is up, switching to STA-only to save power");
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot switch to STA-only: %s", esp_err_to_name(err));
    }
}

static void ap_on_timer_cb(void *arg)
{
    if (s_has_ip) {
        return;
    }

    ESP_LOGW(TAG, "no IP for a while, bringing the config AP back");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot switch to AP+STA: %s", esp_err_to_name(err));
        /* without the AP the device is unreachable: try again (stops once an IP is back) */
        esp_timer_start_once(s_ap_on_timer, AP_MODE_RETRY_DELAY_US);
    }
}

/* Station got an IP: let the AP go after a grace period. */
static void ap_policy_on_connected(void)
{
    if (!s_sta_enabled) {
        return; /* AP-only: it is the only way in, never switch it off */
    }
    esp_timer_stop(s_ap_on_timer);  /* fails harmlessly if the timer is not running */
    esp_timer_stop(s_ap_off_timer);
    esp_timer_start_once(s_ap_off_timer, AP_KEEP_AFTER_IP_US);
}

/* Station lost its IP: if that lasts, bring the AP back. Called on every failed retry, so the
 * countdown is only started once. */
static void ap_policy_on_disconnected(void)
{
    if (!s_sta_enabled) {
        return;
    }
    esp_timer_stop(s_ap_off_timer);
    if (!esp_timer_is_active(s_ap_on_timer)) {
        esp_timer_start_once(s_ap_on_timer, AP_RETURN_AFTER_LOSS_US);
    }
}

/* ---- event handling --------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "station started, connecting");
        reconnect_timer_cb(NULL);
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *info = data;
        ESP_LOGW(TAG, "station disconnected (reason %d)", info->reason);
        s_has_ip = false;
        ap_policy_on_disconnected();
        schedule_reconnect();
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED: {
        const wifi_event_ap_staconnected_t *info = data;
        ESP_LOGI(TAG, "AP: client " MACSTR " joined", MAC2STR(info->mac));
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *info = data;
        ESP_LOGI(TAG, "AP: client " MACSTR " left", MAC2STR(info->mac));
        break;
    }

    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *info = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&info->ip_info.ip));
        start_sntp_once(); // correct the time for once
        s_retry_count = 0;
        s_has_ip = true;
        ap_policy_on_connected();
    } else if (id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "lost IP");
        s_has_ip = false;
        ap_policy_on_disconnected();
    }
}

/* ---- public API ------------------------------------------------------------------------- */

esp_err_t wifi_mgr_start(void)
{
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "already started");
    s_started = true;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed"); /* idempotent */

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_reconnect_timer), TAG,
                        "cannot create reconnect timer");

    const esp_timer_create_args_t ap_off_args = {
        .callback = ap_off_timer_cb,
        .name = "wifi_ap_off",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ap_off_args, &s_ap_off_timer), TAG,
                        "cannot create AP-off timer");
    const esp_timer_create_args_t ap_on_args = {
        .callback = ap_on_timer_cb,
        .name = "wifi_ap_on",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ap_on_args, &s_ap_on_timer), TAG,
                        "cannot create AP-on timer");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG,
        "cannot register Wi-Fi handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL), TAG,
        "cannot register GOT_IP handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, ip_event_handler, NULL), TAG,
        "cannot register LOST_IP handler");

    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_FAIL, TAG,
                        "cannot create STA netif");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap() != NULL, ESP_FAIL, TAG,
                        "cannot create AP netif");

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    /* We persist credentials ourselves; do not let the driver keep a second copy in NVS. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage failed");

    char ssid[WIFI_MGR_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_MGR_PASSWORD_MAX_LEN + 1] = {0};
    esp_err_t cred_err = load_credentials(ssid, sizeof(ssid), password, sizeof(password));
    bool sta_enabled = (cred_err == ESP_OK);
    s_sta_enabled = sta_enabled;
    if (cred_err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no saved Wi-Fi credentials, AP-only mode");
    } else if (cred_err != ESP_OK) {
        ESP_LOGW(TAG, "cannot read Wi-Fi credentials (%s), AP-only mode",
                 esp_err_to_name(cred_err));
    }

    wifi_config_t ap_config = {
        .ap =
            {
                .ssid = CONFIG_AUTODOOR_AP_SSID,
                .password = CONFIG_AUTODOOR_AP_PASSWORD,
                .max_connection = 1,
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP), TAG,
                        "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "AP config failed");

    if (sta_enabled) {
        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, ssid, strnlen(ssid, sizeof(sta_config.sta.ssid)));
        memcpy(sta_config.sta.password, password,
               strnlen(password, sizeof(sta_config.sta.password)));
        /* Only used with WIFI_PS_MAX_MODEM: wake every N beacons instead of every DTIM. */
        sta_config.sta.listen_interval = CONFIG_AUTODOOR_WIFI_LISTEN_INTERVAL;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG,
                            "STA config failed");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    /* Deepest modem sleep for the station. The AP half (while it is up) is unaffected. */
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "cannot enable modem sleep: %s", esp_err_to_name(ps_err));
    }
    ESP_LOGI(TAG, "started in %s mode", sta_enabled ? "AP+STA" : "AP-only");
    return ESP_OK;
}

bool wifi_mgr_has_ip(void)
{
    return s_has_ip;
}
