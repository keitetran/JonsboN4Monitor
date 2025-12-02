set(GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../generated")
set(CUSTOM_DIR "${CMAKE_CURRENT_LIST_DIR}/../custom")

file(GLOB_RECURSE GENERATED_SRCS
    "${GENERATED_DIR}/*.c"
)

file(GLOB_RECURSE CUSTOM_SRCS
    "${CUSTOM_DIR}/*.c"
)

set(PROJECT_SRCS
    ${GENERATED_SRCS}
    ${CUSTOM_SRCS}
)

idf_component_register(
    SRCS ${PROJECT_SRCS}
    INCLUDE_DIRS
        "${GENERATED_DIR}"
        "${GENERATED_DIR}/guider_fonts"
        "${GENERATED_DIR}/guider_customer_fonts"
        "${GENERATED_DIR}/images"
        "${CUSTOM_DIR}"
    REQUIRES
        lvgl
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_lcd_jd9365_10_1
        esp_lcd_st7701
        esp_driver_ppa
        esp_mm
        esp_hw_support
        driver
        esp_timer
        freertos
        esp_tinyusb
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        mqtt
        app_update
        esp_https_ota
        esp_http_client
)

