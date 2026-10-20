cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
# list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs")

# One lock file per target: the component manager records IDF_TARGET in the
# lock, so a shared file flips between esp32 and esp32s3 on every build.
idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_SOURCE_DIR}/dependencies.lock.${IDF_TARGET}")

project(firmware_espidf)