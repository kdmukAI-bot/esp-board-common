cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BOARD)
    set(BOARD "waveshare_p4_lcd43")
endif()
message(STATUS "Building for board: ${BOARD}")

# esp-board-common component (two levels up). The touch test needs no camera, so neither
# its components/ (board_camera) nor the camera pipeline is on the search path.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

set(BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../boards/${BOARD}")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${BOARD_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(touch_test)
