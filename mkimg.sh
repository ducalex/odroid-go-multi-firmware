#!/bin/bash
RELEASE=`date +%Y%m%d`;
BINNAME=odroid-go-multi-firmware

./tools/mkimg.py "odroid-go-multi-firmware-$RELEASE.img" \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partitions.bin \
    0xf000 build/phy_init_data.bin \
    0x10000 build/$BINNAME.bin #\
    #0xE0000 dummy.bin
