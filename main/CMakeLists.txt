idf_component_register(
    SRCS
        "main.cpp"
        "app_settings.cpp"
        "app_fs.cpp"
        "app_wifi.cpp"
        "app_serial.cpp"
        "app_mqtt.cpp"
        "app_email.cpp"
        "app_ota.cpp"
        "app_httpd.cpp"
        "sbms_json.cpp"
        "cell_minmax.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        mqtt
        esp-tls
        mbedtls
        lwip
        app_update
        esp_partition
        spi_flash
        spiffs
        esp_timer
        esp_hw_support
        esp_driver_uart
        esp_driver_gpio
        jsvarstore
        sbmsdata
)

# Firmware version from git (replaces genVersion.py)
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "undefined version")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GIT_VERSION="${GIT_VERSION}"
    ARDUINOJSON_ENABLE_ARDUINO_STRING=0
    ARDUINOJSON_ENABLE_STD_STRING=1
    ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
    ARDUINOJSON_ENABLE_ARDUINO_PRINT=0
    ARDUINOJSON_ENABLE_PROGMEM=0
)
