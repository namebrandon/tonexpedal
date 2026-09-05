idf_component_register(
    SRCS
        "../../src/main.cpp"
        "../../src/led_status.cpp"
        "../../src/tonex_hdlc.cpp"
        "../../src/tonex_usb_host.cpp"
        "../../src/wifi_config.cpp"
        "../../src/ws_bridge.cpp"
    INCLUDE_DIRS "../../include"
    REQUIRES
        arduino
        esp_wifi
        usb
        bblanchon__arduinojson
        tonex_async_web
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BOARD_HAS_PSRAM
    TONEX_BOARD_P4_WIFI6=1
    TONEX_MDNS_NAME=\"tonex\"
)

littlefs_create_partition_image(littlefs ../../data FLASH_IN_PROJECT)
