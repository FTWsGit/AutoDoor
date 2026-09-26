idf_component_register(SRCS "mqtt_mgr.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event espressif__mqtt
                    PRIV_REQUIRES servo)
