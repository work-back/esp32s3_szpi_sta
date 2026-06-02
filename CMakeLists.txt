# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# run auto update build version
find_program(PYTHON_BIN NAMES python3 python REQUIRED)
set(UPDATE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/update_build_version.py")
execute_process(
    COMMAND ${PYTHON_BIN} ${UPDATE_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE update_res
    ERROR_VARIABLE update_err
)

if(NOT update_res EQUAL 0)
    message(FATAL_ERROR "run update_build_version.py failed: ${update_err}")
else()
    message(STATUS "run update_build_version.py SUCCESS")
endif()

list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/build_version.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_sta)

target_sources(app PRIVATE src/main.c src/ble_rc.c src/fatfs.c src/http_dl.c src/sha256sum.c src/upgrade.c src/sd_speed.c)

target_sources_ifdef(CONFIG_WIFI_STA_EN app PRIVATE src/wscli.c src/sta.c src/rpc_route.c src/rpc_send.c src/http_client.c)
