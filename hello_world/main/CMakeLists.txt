idf_component_register(SRCS
    "main.c"
    "src/wifi_connect.c"
    "src/tcp_server_task.c"
    "src/ir_led_task.c"
    "src/message_type.c"
    "src/frame_maker_task.c"
    "src/infoglobe_animations.c"
    "src/sntp_helper.c"
    "src/tcp_helpers.c"
    "src/http_server_task.c"
    INCLUDE_DIRS "" "inc"
    REQUIRES
        driver
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        protocol_examples_common
        lwip
        esp_http_server
        json
        app_update
        spiffs
        infoglobe_datatype)

spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)

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
