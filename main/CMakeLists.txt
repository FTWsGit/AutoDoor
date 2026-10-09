idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES nvs_flash esp_event esp_netif
                                     door wifi_mgr mqtt_mgr web_ui health)
