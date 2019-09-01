# Description
This is a mod of odroid-go-firmware. It allows to keep installed multiple applications at once, allowing you to switch instantly between them.

# Installation

This method will replace the stock firmware.

Follow these instructions: https://wiki.odroid.com/odroid_go/firmware_update but use the .img provided here.

*Note: To preserve your flashed applications when upgrading you must ignore the ERASE step.*

To get rid of it follow the instructions again using their official .img file.

To access the boot menu you then hold **B** while booting, as before.


# Questions

> **Q: How does it work?**
>
> A: When you select an application to boot, the ESP32's partition table is rewritten to make it seem like the app is the only thing there. This firmware keeps track of what is installed and where. The app will then boot every time you power up the system, until you change it.

> **Q: Why rewrite the partition table when booting an application, why not keep all partitions of all apps?**
>
> A: I tried. It causes conflicts with subtypes. Renumbering them mostly worked with single-partition applications. Multi-partition applications (like Go-Play) however mostly did not work because they (understandably) expect the types they declared when packaging.

> **Q: How can I get the boot menu without holding a button for 2 seconds?**
>
> A: Unfortunately, I haven't found a reliable way of doing that yet


# To do / Ideas

- XModem/YModem protocol to transfer applications via USB

- Add back test partition flashing (utility.bin)

- Copy a flashed firmware back to the sd card as a .fw

- Brightness control


# Compilation
The official esp-idf version 3.1 or newer is required and you need to apply the following patches:

- [Allow clearing internal partition list](https://github.com/OtherCrashOverride/esp-idf/commit/49fbef73c300920d2f63c9afb705eefabe3dac87) (Required)
- [Improve SD card compatibility](https://github.com/OtherCrashOverride/esp-idf/commit/a83e557538a033e25c376eedac79663c9b7b75da) (Recommended)

You can also find the patches in tools/patches.