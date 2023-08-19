idf_component_register(SRCS "wifi_connect.c" "hello_world_main.c" "src/tcp_server_task.c" "src/ir_led_task.c" "src/message_type.c"
                    INCLUDE_DIRS "" "inc")

set_source_files_properties("wifi_connect.c" "hello_world_main.c" "src/tcp_server_task.c" "src/ir_led_task.c" "src/message_type.c"
    PROPERTIES COMPILE_FLAGS
    --coverage)
