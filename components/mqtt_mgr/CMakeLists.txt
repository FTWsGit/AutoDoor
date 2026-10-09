idf_component_register(SRCS "mqtt_mgr.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES espressif__mqtt esp_event esp_netif)
