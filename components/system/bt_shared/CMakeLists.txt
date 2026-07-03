idf_component_register(
    SRCS "bt_shared.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bt nvs_flash
)
