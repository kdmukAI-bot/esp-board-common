# Image-entropy consumer test app: camera pipeline + entropy consumer.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-board-common" "board_camera" "board_log_flash" "espressif__esp_lvgl_port" "esp-camera-pipeline" "cam_pipeline_entropy" "esp_lcd" "mbedtls"
)
