idf_component_register(SRCS "./src/ssh_task.c" "./src/ssh_driver.c"
                    INCLUDE_DIRS . "./inc/"
                    PRIV_REQUIRES driver console libssh2
                    REQUIRES )
