idf_component_register(
    SRCS "wifi_shared.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash os_fs
)
