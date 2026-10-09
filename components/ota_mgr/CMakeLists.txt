# The CA that signed the firmware server's certificate is embedded into the image.
# certs/ is git-ignored (it may also hold private keys), so each developer provides it.
get_filename_component(ca_crt "${CMAKE_CURRENT_LIST_DIR}/../../certs/ca.crt" ABSOLUTE)
if(NOT EXISTS "${ca_crt}")
    message(FATAL_ERROR
        "OTA CA certificate not found: ${ca_crt}\n"
        "Create it as described in README.md (section 'OTA updates').")
endif()

idf_component_register(SRCS "ota_mgr.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_https_ota esp_http_client app_update esp_app_format
                                     esp_system
                       EMBED_TXTFILES "${ca_crt}")
