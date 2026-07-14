idf_component_register(
    SRCS "weather_shared.c"
    INCLUDE_DIRS "include"
    REQUIRES os_fs wifi_shared esp_http_client esp-tls mbedtls esp_timer
)
