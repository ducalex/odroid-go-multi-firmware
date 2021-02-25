# Description
Odroid-go-multi-firmware is an improvement of the official Odroid GO firmware. It allows you to keep multiple applications installed in the flash and switch instantly between them. You can think of it as a multi-boot loader.


# Installation

Follow these instructions: https://wiki.odroid.com/odroid_go/firmware_update but use the .img provided here.

*Note: To preserve your flashed applications when upgrading you can ignore the ERASE step.*

To access the boot menu you then hold **B** while booting, as before.

_Note: There is no risk in flashing your Odroid GO and you can easily return to the stock firmware by following the instructions again using their official .img file._


# Compilation
The official esp-idf version 3.1 or newer is required and you need to apply the following patches:

- [Allow clearing internal partition list](https://github.com/OtherCrashOverride/esp-idf/commit/49fbef73c300920d2f63c9afb705eefabe3dac87) (Required)
- [Improve SD card compatibility](https://github.com/OtherCrashOverride/esp-idf/commit/a83e557538a033e25c376eedac79663c9b7b75da) (Recommended)

You can also find the patches in the tools/patches folder.

_Note: Those patches do not introduce breaking changes to non-GO (standard ESP32) projects and can safely be applied to your global esp-idf installation._

_Note2: esp-idf newer than 3.3-rc wil cause compatibility issues with older apps like Go-Play. The version I use to build official releases is `v3.3-rc-8-gbeb34b539`._


## Build Steps:
1. Compile firmware: `make -j4` or `idf.py build`
2. And finally:
   - To produce .img: `./mkimg.sh`
   - To flash and debug: `make flash monitor`

# Technical information

### Creating .fw files
The mkfw.py tool is used to package your application in a .fw file.

Usage:    
`mkfw.py output_file.fw 'description' tile.raw type subtype size label file.bin [type subtype size label file.bin, ...]`

- tile.raw must be a RAW RGB565 86x48 image

- [type](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html#type) is 0 for application and 1 for data
- [subtype](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html#subtype) can be set to 0 for auto
- [size](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html#offset-size) must be equal to or bigger than your app's .bin and a multiple of 65536. Can be set to 0 for auto
- [label](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html#name-field) is a label for your own usage
- file.bin contains the partition's data

### .fw format:
```
 Header:
   "ODROIDGO_FIRMWARE_V00_01"  24 bytes
   Firmware Description        40 bytes
   RAW565 86x48 tile           8256 bytes
 Partition [, ...]:
   Type                        1 byte
   Subtype                     1 byte
   Padding                     2 bytes
   Label                       16 bytes
   Flags                       4 bytes
   Size                        4 bytes
   Data length                 4 bytes
   Data                        <Data length> bytes
 Footer:
   CRC32                       4 bytes
```

# Questions

> **Q: How does it work?**
>
> A: When you select an application to boot, the ESP32's partition table is rewritten to make it seem like the app is the only thing there. That way any existing, unmodified, Odroid-GO applications will work. The firmware keeps track of what is installed and where. The app will then boot every time you power up the system, until you change it.
