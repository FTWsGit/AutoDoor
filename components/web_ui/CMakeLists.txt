idf_component_register(SRCS "web_ui.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_https_server esp_timer esp_system app_update
                                     esp_app_format wifi_mgr ota_mgr
                       EMBED_TXTFILES "web/index.html" "../../certs/esp_server.crt" "../../certs/esp_server.key")
