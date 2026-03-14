idf_component_register(SRCS "sd_mount.c" "sd_logger.c"
                    INCLUDE_DIRS "."
                    REQUIRES Player fatfs ld_core)
