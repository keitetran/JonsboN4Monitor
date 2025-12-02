# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

set(_idf_path "")
if(DEFINED IDF_PATH AND NOT "${IDF_PATH}" STREQUAL "")
    set(_idf_path "${IDF_PATH}")
elseif(DEFINED ENV{IDF_PATH} AND NOT "$ENV{IDF_PATH}" STREQUAL "")
    set(_idf_path "$ENV{IDF_PATH}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/esp-idf")
    set(_idf_path "${CMAKE_CURRENT_LIST_DIR}/esp-idf")
endif()

if(_idf_path STREQUAL "")
    message(FATAL_ERROR "IDF_PATH is not set. Please:\n"
        "  1. Run ESP-IDF export script: C:/Espressif/frameworks/esp-idf-v5.5/export.bat\n"
        "  2. Or use idf.py to build (recommended): idf.py build\n"
        "  3. Or set IDF_PATH: -DIDF_PATH=C:/Espressif/frameworks/esp-idf-v5.5")
endif()

file(TO_CMAKE_PATH "${_idf_path}" _idf_path_normalized)
if(NOT EXISTS "${_idf_path_normalized}/tools/cmake/project.cmake")
    message(FATAL_ERROR "ESP-IDF entry point not found: ${_idf_path_normalized}/tools/cmake/project.cmake")
endif()


include("${_idf_path_normalized}/tools/cmake/project.cmake")

# Set project version (used for OTA update comparison)
# Update this version when releasing new firmware
set(PROJECT_VER "1.0.1")

project(JonsboN4Monitor)
