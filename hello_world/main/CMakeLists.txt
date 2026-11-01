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
