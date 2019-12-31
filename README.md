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
> A: When you select an application to boot, the ESP32's partition table is rewritten to make it seem like the app is the only thing there. This firmware keeps track of what is installed and where. The app will then boot every time you power up the system, until you change it.

> **Q: Why rewrite the partition table when booting an application, why not keep all partitions of all apps?**
>
> A: I tried. It causes conflicts with subtypes. Single-partition applications worked fine when renumbering the partitions but multi-partition applications (like Go-Play) did not work correctly because they can no longer locate their own partitions.

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
1. Compile mkimg: `cd tools/mkimg && make`
2. Compile firmware: `cd .. && make -j4`
3. And finally:
   - To produce .img: `./mkimg.sh`
   - To flash and debug: `make flash monitor`
