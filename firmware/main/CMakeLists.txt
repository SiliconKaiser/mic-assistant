idf_component_register(
    SRCS "main.c" "power.c" "battery.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_timer
)
