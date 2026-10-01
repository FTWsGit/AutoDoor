idf_component_register(SRCS "http_server.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash esp_http_server)
