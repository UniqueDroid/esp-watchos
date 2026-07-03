idf_component_register(
    SRCS "webserver_shared.c"
    INCLUDE_DIRS "include"
    REQUIRES os_fs esp_http_server display_shared homescreen_shared
)
