cmake_minimum_required(VERSION 3.16)

# Board selection: idf.py -DBOARD=prototype build   (default: prototype)
if(NOT DEFINED BOARD)
    set(BOARD prototype)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/sdkconfig.defaults")
    message(FATAL_ERROR "Unknown BOARD '${BOARD}' (see firmware/boards/)")
endif()

# Common settings first, then the board's overrides.
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/sdkconfig.defaults")
# Separate sdkconfig and build dir per board so switching boards doesn't mix settings.
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${BOARD}")
set(IDF_TARGET esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mic-assistant)
