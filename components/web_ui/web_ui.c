#include "web_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "ota_mgr.h"
#include "wifi_mgr.h"

#define BODY_MAX_LEN       512
#define FORM_VALUE_MAX_LEN 64 /* percent-encoded; 64 characters can take up to 192 bytes */
#define MAX_RECV_TIMEOUTS  3
#define HTTPD_STACK_SIZE   6144
#define RESTART_DELAY_MS   1000

extern const char index_html_start[] asm("_binary_index_html_start");

static const char *TAG = "web_ui";
static httpd_handle_t s_server;

/* ---- request helpers -------------------------------------------------------------------- */

/** Receive the whole request body (must fit in `cap - 1` bytes) and NUL-terminate it. */
static esp_err_t read_body(httpd_req_t *req, char *buf, size_t cap)
{
    size_t total = req->content_len;
    if (total == 0 || total >= cap) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    int timeouts = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > MAX_RECV_TIMEOUTS) {
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    buf[received] = '\0';
    return ESP_OK;
}

/** In-place application/x-www-form-urlencoded decoding. */
static void percent_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            const char hex[3] = {s[1], s[2], '\0'};
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *out++ = ' ';
            s++;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

/** Extract and decode one field of a urlencoded body. */
static esp_err_t form_get(const char *body, const char *key, char *out, size_t out_size)
{
    esp_err_t err = httpd_query_key_value(body, key, out, out_size);
    if (err == ESP_OK) {
        percent_decode(out);
    }
    return err;
}

static void restart_timer_cb(void *arg)
{
    esp_restart();
}

/** Restart after a delay so that the HTTP response can still be flushed to the client. */
static void restart_later(uint32_t delay_ms)
{
    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {.callback = restart_timer_cb, .name = "restart"};

    if (timer == NULL) {
        esp_err_t err = esp_timer_create(&args, &timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot schedule restart (%s), please reboot manually",
                     esp_err_to_name(err));
            return;
        }
    }
    esp_timer_start_once(timer, (uint64_t)delay_ms * 1000);
}

/* ---- handlers --------------------------------------------------------------------------- */

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t info_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    char json[256];
    snprintf(json, sizeof(json),
             "{\"chip\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\",\"partition\":\"%s\"}",
             CONFIG_IDF_TARGET, app->version, app->idf_ver, running ? running->label : "?");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t wifi_sta_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX_LEN];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "长度非法或接收失败");
        return ESP_FAIL; /* close the socket: an unread body would corrupt the next request */
    }

    char ssid[FORM_VALUE_MAX_LEN];
    char passwd[FORM_VALUE_MAX_LEN];
    if (form_get(body, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        form_get(body, "passwd", passwd, sizeof(passwd)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "缺少或超长的 ssid / passwd");
        return ESP_OK;
    }

    esp_err_t err = wifi_mgr_set_credentials(ssid, passwd);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID 或密码长度不合法 (密码需 8-64 位)");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot store Wi-Fi credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
        return ESP_OK;
    }

    char resp[96];
    snprintf(resp, sizeof(resp), "已保存 SSID=%s, 重启中...", ssid);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t send_err = httpd_resp_sendstr(req, resp);

    ESP_LOGI(TAG, "credentials saved, restarting");
    restart_later(RESTART_DELAY_MS);
    return send_err;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    char body[BODY_MAX_LEN];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "长度非法或接收失败");
        return ESP_FAIL;
    }

    char uri[FORM_VALUE_MAX_LEN];
    if (form_get(body, "ota_uri", uri, sizeof(uri)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "缺少 ota_uri");
        return ESP_OK;
    }

    esp_err_t err = ota_mgr_start(uri);
    switch (err) {
    case ESP_OK:
        ESP_LOGI(TAG, "OTA accepted");
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_sendstr(req, "OTA 已开始");
    case ESP_ERR_INVALID_ARG:
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "固件地址必须是 https:// 开头 (最长 255 字符)");
        break;
    case ESP_ERR_INVALID_STATE:
        /* esp_http_server has no HTTPD_409_* constant, so send the status line by hand. */
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "已有升级正在进行");
        break;
    default:
        ESP_LOGE(TAG, "cannot start OTA: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法启动升级");
        break;
    }
    return ESP_OK;
}

static esp_err_t ota_status_get_handler(httpd_req_t *req)
{
    ota_mgr_status_t status;
    ota_mgr_get_status(&status);

    char json[128];
    snprintf(json, sizeof(json), "{\"state\":\"%s\",\"progress\":%u,\"error\":\"%s\"}",
             ota_mgr_state_name(status.state), status.progress_pct,
             esp_err_to_name(status.last_error));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

/* ---- public API ------------------------------------------------------------------------- */

static const httpd_uri_t s_routes[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_get_handler},
    {.uri = "/info", .method = HTTP_GET, .handler = info_get_handler},
    {.uri = "/wifi_sta", .method = HTTP_POST, .handler = wifi_sta_post_handler},
    {.uri = "/ota", .method = HTTP_POST, .handler = ota_post_handler},
    {.uri = "/ota/status", .method = HTTP_GET, .handler = ota_status_get_handler},
};

esp_err_t web_ui_start(void)
{
    ESP_RETURN_ON_FALSE(s_server == NULL, ESP_ERR_INVALID_STATE, TAG, "already running");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = HTTPD_STACK_SIZE;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start failed");

    for (size_t i = 0; i < sizeof(s_routes) / sizeof(s_routes[0]); i++) {
        esp_err_t err = httpd_register_uri_handler(server, &s_routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot register %s: %s", s_routes[i].uri, esp_err_to_name(err));
            httpd_stop(server);
            return err;
        }
    }

    s_server = server;
    ESP_LOGI(TAG, "listening on port %d", config.server_port);
    return ESP_OK;
}

bool web_ui_is_running(void)
{
    return s_server != NULL;
}
