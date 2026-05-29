#/bin/bash

ESP_DEV=/dev/ttyACM0
# BOARD=esp32s3_szpi/esp32s3/procpu
BOARD=esp32s3_wave/esp32s3/procpu
#BOARD=esp32c6_wave/esp32c6/hpcore
PRJ=sta

build_all() {
    if [ ! -z $1 ] ; then
        PRJ=$1
    fi
    west build -p always -b ${BOARD} --sysbuild ${PRJ}
}

build_all_no_net() {
    if [ ! -z $1 ] ; then
        PRJ=$1
    fi
    west build -p always -b ${BOARD} --sysbuild ${PRJ} -- -DCONF_FILE=prj_no_net.conf
}

flash_esp() {
    west flash --esp-device ${ESP_DEV}
}

monitor_esp() {
    west espressif monitor -p ${ESP_DEV}
}

erase_and_flash_esp() {
    west flash --erase --esp-device ${ESP_DEV}
}

esp_auto() {
    build_all
    flash_esp
    monitor_esp
}

