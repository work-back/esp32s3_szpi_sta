#!/bin/bash

sudo systemctl stop bluetooth
sudo hciconfig hci0 down

EXE_PATH=/home/langyj/w2/zephyr/project/myprj/build-sim_rc/zephyr/zephyr.exe
sudo setcap 'cap_net_raw,cap_net_admin+ep' ${EXE_PATH}
${EXE_PATH} --bt-dev=hci0
