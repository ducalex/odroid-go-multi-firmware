#!/bin/bash
RELEASE=`date +%Y%m%d`;
BINNAME=odroid-go-firmware

#ffmpeg -vcodec png -i tile.png -vcodec rawvideo -f rawvideo -pix_fmt rgb565 tile.raw

# ./tools/mkfw/mkfw "Firmware Manager ($RELEASE)" tile.raw \
#     0 32 851968 manager build/$BINNAME.bin \
#     1 254 131072 apptable empty.bin \
#     0 16 65536 empty empty.bin

# mv firmware.fw "odroid-go-firmware-$RELEASE.fw"

./tools/mkimg/mkimg "odroid-go-firmware-$RELEASE.img" \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partitions.bin \
    0xf000 build/phy_init_data.bin \
    0x10000 build/$BINNAME.bin #\
    #0xE0000 dummy.bin

