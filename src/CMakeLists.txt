file(GLOB SOURCES "*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "../include"
    REQUIRES esp_wifi
             nvs_flash
             freertos
             esp_driver_uart
)