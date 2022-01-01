# Description
Odroid-go-multi-firmware is an improvement of the official Odroid GO firmware. It allows you to keep multiple applications installed in the flash and switch instantly between them. You can think of it as a multi-boot loader.


# Usage
Holding **B** while powering up the device will bring you to the boot menu.


# Installation

_Note: There is no risk in flashing your Odroid GO. You can always recover and return to the stock firmware by following ODROID's guide (method 2 below)._

### Method 1: Self-installer

This is the easiest method, no need for drivers or flashing software. But it isn't as well tested.

1. Copy the file `odroid-go-multi-firmware-XXX.fw` to your sdcard in the folder `/odroid/firmware`
2. Power up your device while holding B
3. Flash the self-installer and run it


### Method 2: USB flashing

Follow these instructions: https://wiki.odroid.com/odroid_go/firmware_update but use the .img provided here.

_Note: If you are familiar with esptool, you just have to flash `odroid-go-multi-firmware-XXX.fw` to offset 0x0._


# Compilation
The official esp-idf version 3.1 or newer is required and to improve SD Card support you should this patch:

- tools/patches/esp-idf-sdcard-patch.diff

_Note: Those patches do not introduce breaking changes to non-GO (standard ESP32) projects and can safely be applied to your global esp-idf installation._

## Build Steps:
1. Compile firmware: `idf.py build`
2. And then:
   - To produce image files: `python tools/pack.py`
   - To flash and debug: `idf.py flash monitor`


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
