#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "string.h"

#include "http_server.h"
#include "index_html.h"

const char *TAG = "HTTP_SERVER";

static void url_decode(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (*src && i < max - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char) strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// GET /
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char *html = INDEX_HTML;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// POST /wifi_sta
static esp_err_t wifi_sta_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "长度非法");
        return ESP_FAIL;
    }

    char *buf = (char*)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "接收失败");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';
    ESP_LOGI(TAG, "Got raw body: %s", buf);

    char ssid_raw[33] = {0}, passwd_raw[65] = {0};
    char ssid[33] = {0}, passwd[65] = {0};

    char *p = strstr(buf, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        int len = end ? (end - p) : (int) strlen(p);
        if (len > (int) sizeof(ssid_raw) - 1) len = sizeof(ssid_raw) - 1;
        strncpy(ssid_raw, p, len);
    }
    p = strstr(buf, "passwd=");
    if (p) {
        p += 7;
        char *end = strchr(p, '&');
        int len = end ? (end - p) : (int) strlen(p);
        if (len > (int) sizeof(passwd_raw) - 1) len = sizeof(passwd_raw) - 1;
        strncpy(passwd_raw, p, len);
    }

    url_decode(ssid,   ssid_raw,   sizeof(ssid));
    url_decode(passwd, passwd_raw, sizeof(passwd));

    ESP_LOGI(TAG, "received SSID=%s  PASSWD=%s", ssid, passwd);

    nvs_handle_t nvs_handle;
    esp_err_t err;
    err = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to open nvs: %s", err);
        ESP_ERROR_CHECK(err);
    }

    bool isSuccess = true;

    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to set wifi SSID: %s", err);
        isSuccess = false;
    } else {
        ESP_LOGI(TAG, "set wifi SSID: %s", ssid);
    }

    err = nvs_set_str(nvs_handle, "password", passwd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to set wifi password: %s", err);
        isSuccess = false;
    } else {
        ESP_LOGI(TAG, "set wifi password: %s", passwd);
    }

    nvs_close(nvs_handle);

    char resp[256];
    if (isSuccess) {
        snprintf(resp, sizeof(resp), "已保存 SSID=%s, 重启中...", ssid);
    }
    else {
        snprintf(resp, sizeof(resp), "保存 SSID=%s 失败, 重启中...", ssid);
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    free(buf);

    ESP_LOGI(TAG, "restart the device now...");
    esp_restart();

    return ESP_OK;
}


void http_server_start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // Default 80 port
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .method = HTTP_GET,
            .uri = "/",
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t wifi_sta_uri = {
            .method = HTTP_POST,
            .uri = "/wifi_sta", 
            .handler = wifi_sta_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_sta_uri);

        ESP_LOGI(TAG, "HTTP server started on port 80");
    }
    else {
        ESP_LOGW(TAG, "failed to start HTTP server");
    }
}
