idf_component_register(SRCS
    "main.c"
    "src/tcp_server_task.c"
    "src/ir_led_task.c"
    "src/message_type.c"
    "src/frame_maker_task.c"
    "src/infoglobe_animations.c"
    "src/sntp_helper.c"
    "src/tcp_helpers.c"
    "src/http_server_task.c"
    "src/network.c"
    "src/globe_frame.c"
    "src/globe_effects.c"
    "src/api_v1.c"
    "src/scene_engine.c"
    "src/clock_values.c"
    "src/value_store.c"
    "src/live_values.c"
    "src/http_fetch.c"
    "src/feeds.c"
    "src/feed_weather.c"
    "src/feed_aircraft.c"
    INCLUDE_DIRS "" "inc"
    REQUIRES
        esp_driver_rmt
        esp_driver_gpio
        esp_wifi
        esp_eth
        esp_timer
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_http_server
        esp_http_client
        mbedtls
        esp_hw_support
        app_update
        spiffs
        infoglobe_datatype)

# Web designer (../web, Vite + Preact): rebuilt into data/designer.html.gz when
# its sources change, if Node is installed and `npm install` has been run.
# The built file is committed, so the firmware still builds without Node.
set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../web)
set(DESIGNER_GZ ${CMAKE_CURRENT_LIST_DIR}/data/designer.html.gz)
find_program(NPM_EXE NAMES npm.cmd npm)
set(SPIFFS_EXTRA_DEPENDS "")
if(NPM_EXE AND EXISTS ${WEB_DIR}/node_modules)
    file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS ${WEB_DIR}/src/*)
    add_custom_command(OUTPUT ${DESIGNER_GZ}
        COMMAND ${NPM_EXE} run build
        WORKING_DIRECTORY ${WEB_DIR}
        DEPENDS ${WEB_SOURCES} ${WEB_DIR}/index.html ${WEB_DIR}/vite.config.js
                ${CMAKE_CURRENT_LIST_DIR}/../../docs/effects.json
        COMMENT "Building the web designer (npm run build)"
        VERBATIM)
    add_custom_target(web_designer DEPENDS ${DESIGNER_GZ})
    set(SPIFFS_EXTRA_DEPENDS DEPENDS web_designer)
endif()

spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT ${SPIFFS_EXTRA_DEPENDS})

# set_source_files_properties(
#     "main.c" 
#     "src/wifi_connect.c"
#     "src/tcp_server_task.c"
#     "src/ir_led_task.c"
#     "src/message_type.c"
#     "src/frame_maker_task.c"
#     "src/infoglobe_animations.c"
#     "src/sntp_helper.c"
#     "src/ota_server_task.c"
#     "src/button_tcp_task.c"
#     PROPERTIES COMPILE_FLAGS
#     --coverage)
