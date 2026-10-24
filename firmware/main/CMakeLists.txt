idf_component_register(
    SRCS "main.cpp" "power.cpp" "battery.cpp" "led.cpp" "mic.cpp" "amp.cpp" "dump.cpp" "wifi.cpp" "session.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_timer esp_driver_usb_serial_jtag esp_wifi esp_netif esp_event nvs_flash json lwip
)

# ESP-IDF config structs are filled with designated initializers that leave fields out on
# purpose (they're zero-initialized). C++ warns about that, C doesn't; keep the C habit.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
