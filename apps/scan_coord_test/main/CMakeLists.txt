# Scan coordinator test app: camera pipeline + QR consumer from esp-camera-pipeline.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-board-common" "board_camera" "board_log_flash" "espressif__esp_lvgl_port" "esp-camera-pipeline" "cam_pipeline_qr" "esp_lcd"
)
