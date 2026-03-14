idf_component_register(
    SRCS "src/sd_writer.cpp" "src/tcp_client.cpp" 

    INCLUDE_DIRS "include"

    REQUIRES fatfs sdmmc esp_wifi lwip nvs_flash PT_Reader Messenger SD
)