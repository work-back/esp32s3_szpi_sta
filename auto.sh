#/bin/bash

ESP_DEV=/dev/ttyACM0
BOARD=esp32s3_szpi/esp32s3/procpu
PRJ=sta

build_all() {
    if [ ! -z $1 ] ; then
        PRJ=$1
    fi
    west build -p always -b ${BOARD} --sysbuild ${PRJ}
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

