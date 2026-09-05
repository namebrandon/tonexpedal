file(GLOB async_web_sources "vendor/src/*.cpp")

idf_component_register(
    SRCS ${async_web_sources}
    INCLUDE_DIRS "vendor/src"
    REQUIRES arduino esp_wifi bblanchon__arduinojson tonex_async_tcp
)
