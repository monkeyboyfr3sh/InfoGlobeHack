idf_component_register(SRCS "main.c" "src/wifi_connect.c" "src/tcp_server_task.c" "src/ir_led_task.c" "src/message_type.c" "src/frame_maker_task.c" "src/infoglobe_animations.c" "src/sntp_helper.c" "src/ota_server_task.c"
                    INCLUDE_DIRS "" "inc")

set_source_files_properties("main.c" "src/wifi_connect.c" "src/tcp_server_task.c" "src/ir_led_task.c" "src/message_type.c" "src/frame_maker_task.c" "src/infoglobe_animations.c" "src/sntp_helper.c" "src/ota_server_task.c"
    PROPERTIES COMPILE_FLAGS
    --coverage)
