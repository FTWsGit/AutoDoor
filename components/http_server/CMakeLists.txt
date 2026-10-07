idf_component_register(SRCS "http_server.c"
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES "../../certs/ca.crt"
                    REQUIRES nvs_flash esp_http_server esp_https_ota bootloader_support)
