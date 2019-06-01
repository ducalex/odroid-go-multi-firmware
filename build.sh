#!/bin/bash
release=`date +%Y%m%d`;
./tools/mkfw/mkfw "Firmware Manager ($release)" tile.raw 0 32 851968 manager build/odroid-go-firmware.bin 1 254 131072 apptable dummy.bin 0 16 100 dummy dummy.bin
