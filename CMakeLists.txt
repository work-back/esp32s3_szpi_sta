# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi_sta)

target_sources(app PRIVATE src/main.c src/ble_rc.c src/fatfs.c src/http_dl.c)

target_sources_ifdef(CONFIG_WIFI_STA_EN app PRIVATE src/wscli.c src/sta.c src/rpc_route.c src/rpc_send.c src/http_client.c)
