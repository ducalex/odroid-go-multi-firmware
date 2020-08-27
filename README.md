# Description
Odroid-go-multi-firmware is an improvement of the official Odroid GO firmware. It allows you to keep multiple applications installed in the flash and switch instantly between them. You can think of it as a multi-boot loader.


# Installation

This method will replace the stock firmware.

Follow these instructions: https://wiki.odroid.com/odroid_go/firmware_update but use the .img provided here.

*Note: To preserve your flashed applications when upgrading you can ignore the ERASE step.*

To access the boot menu you then hold **B** while booting, as before.

_Note: There is no risk in flashing your Odroid GO and you can easily return to the stock firmware by following the instructions again using their official .img file._


# Questions

> **Q: How does it work?**
>
> A: When you select an application to boot, the ESP32's partition table is rewritten to make it seem like the app is the only thing there. That way any existing, unmodified, Odroid-GO applications will work. The firmware keeps track of what is installed and where. The app will then boot every time you power up the system, until you change it.

> **Q: How can I get the boot menu without holding a button for 2 seconds?**
>
> A: Unfortunately I haven't found a reliable way of doing that yet (it causes problems with multi-partitions applications).


# To do / Ideas

- Ability to update/flash itself (instead of requiring USB and esptool)
- Copy a flashed application back to the sd card as a .fw
- Brightness control


# Compilation
The official esp-idf version 3.1 or newer is required and you need to apply the following patches:

- [Allow clearing internal partition list](https://github.com/OtherCrashOverride/esp-idf/commit/49fbef73c300920d2f63c9afb705eefabe3dac87) (Required)
- [Improve SD card compatibility](https://github.com/OtherCrashOverride/esp-idf/commit/a83e557538a033e25c376eedac79663c9b7b75da) (Recommended)

You can also find the patches in the tools/patches folder.

_Note: Those patches do not introduce breaking changes to non-GO (standard ESP32) projects and can safely be applied to your global esp-idf installation._


## Build Steps:
1. Compile firmware: `make -j4` or `idf.py build`
2. And finally:
   - To produce .img: `./mkimg.sh`
   - To flash and debug: `make flash monitor`

# Technical information

The mkfw.py tool can be used to generate .fw files. Its usage is identical to Odroid's own C mkfw but
doesn't require a local C compiler.

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
