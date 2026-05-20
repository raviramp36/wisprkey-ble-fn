idf_component_register(
    SRCS "main.c" "esp_hid_gap.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hid
    PRIV_REQUIRES esp_driver_gpio nvs_flash bt
)
