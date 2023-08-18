idf_component_register(SRCS "wifi_connect.c" "hello_world_main.c"
                    INCLUDE_DIRS "")

set_source_files_properties("wifi_connect.c" "hello_world_main.c"
    PROPERTIES COMPILE_FLAGS
    --coverage)
