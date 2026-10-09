idf_component_register(SRCS "web_ui.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server esp_timer esp_system app_update
                                     esp_app_format wifi_mgr ota_mgr
                       EMBED_TXTFILES "web/index.html")
