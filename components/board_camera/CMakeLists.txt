# Camera support for esp-board-common: the DVP and CSI camera drivers, the glue that
# feeds them into esp-camera-pipeline and its LVGL preview, and the QR scan coordinator.
#
# A project that uses the camera puts this directory's parent (esp-board-common's
# components/) and esp-camera-pipeline's nested components/ on EXTRA_COMPONENT_DIRS and
# REQUIREs board_camera. A project with no camera leaves both off, and the camera drivers
# in this component's idf_component.yml are never fetched or built.
#
# The sources are #if BOARD_HAS_CAMERA-guarded, so on a board without a camera (its
# board_config.h defines it 0) they compile to nothing.

# esp-board-common is two directories up. Its component name is its directory name, which
# differs between consumers (esp-board-common in its own apps, board_common in the
# SeedSigner MicroPython builder), so take it from the path. COMPONENT_DIR is set in
# ESP-IDF's early requirements pass too, so both passes agree.
get_filename_component(_bcam_core_dir "${COMPONENT_DIR}/../.." ABSOLUTE)
get_filename_component(_bcam_core_name "${_bcam_core_dir}" NAME)

set(SRC_FILES
    "src/board_camera.c"
    "src/board_camera_csi.c"
    "src/board_pipeline.c"
    "src/board_pipeline_camera_csi.c"
    "src/board_pipeline_camera_dvp.c"
    "src/board_pipeline_display_lvgl.c"
    "src/scan_coordinator.c"
)

set(REQUIRED_COMPONENTS
    "${_bcam_core_name}"
    "esp-camera-pipeline" "cam_pipeline_qr"
    "driver" "esp_lcd" "esp_timer" "espressif__esp_lvgl_port" "lvgl"
)

# The interface drivers are target-conditional: DVP (esp32-camera) for S3 and compatible
# targets, CSI for P4.
#
# On P4 the esp_video V4L2 layer handles CSI + ISP internally. ov02c10 is a standalone
# esp_cam_sensor add-on (the OV02C10 sensor is not in the public esp_cam_sensor
# component); it compiles to an empty library unless the board's sdkconfig sets
# CONFIG_CAMERA_OV02C10, so it is inert on OV5647 boards.
#
# esp_ipa is listed explicitly: esp_video takes it as an OPTIONAL PRIVATE requirement, so
# its headers are not visible transitively, and the CSI driver needs esp_ipa.h to build
# the runtime-retunable AGC configuration.
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND REQUIRED_COMPONENTS
         "esp_driver_cam" "espressif__esp_cam_sensor" "esp_driver_isp" "espressif__esp_video"
         "espressif__esp_ipa" "ov02c10")
else()
    list(APPEND REQUIRED_COMPONENTS "espressif__esp32-camera")
endif()

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "src"
    REQUIRES ${REQUIRED_COMPONENTS}
)

# esp_video's ISP-pipeline entry point (esp_video_isp_pipeline_init) is a linked global,
# but its declaration lives in the component's PRIV_INCLUDE_DIRS. The CSI driver calls it
# directly so it can supply its own IPA configuration instead of the flash-resident one
# esp_video would look up (see board_pipeline_camera_csi.c). Pull that directory in
# rather than re-declaring the prototype locally, so a component bump that changes the
# signature fails the build instead of silently mismatching. Harmless on boards that
# never compile the CSI driver.
if(IDF_TARGET STREQUAL "esp32p4")
    idf_component_get_property(esp_video_dir espressif__esp_video COMPONENT_DIR)
    if(esp_video_dir)
        target_include_directories(${COMPONENT_LIB} PRIVATE "${esp_video_dir}/private_include")
    endif()
endif()
