# Description
This is a mod of odroid-go-firmware. It allows to keep installed multiple applications at once, allowing you to switch instantly between them.

It can replace the stock firmware or it can coexist and be installed like a normal GO .fw app.


# Installation
There are two methods:

## Replace the stock firmware
This method will replace the stock firmware.

Follow these instructions: https://wiki.odroid.com/odroid_go/firmware_update but use the .img provided here.

To get rid of it follow the instructions again using their .img file.

To access the boot menu you then hold **B** while booting.




## Like any other application
This method is easier as you just drop a file on your sd card. You can also still access the Odroid's stock firmware normally by holding B.

Follow these instructions: https://wiki.odroid.com/odroid_go/write_app but use the .fw provided here.

You will get an error about boot. Ignore it. Power down, hold A, power up, and the new firmware should appear!

To get rid of it just flash any other .fw through the *stock* firmware.

To access the boot menu you then hold **A** while booting. 


# Questions

>  **Q: Why is erase very slow sometimes?**
> 
> A: To avoid fragmenting the flash memory we shift all apps to fill the new empty space, which can be very long in some instances. Eventually I hope to improve the process and make defrag run on demand or only when running out of space.

> **Q: How does it work?**
> 
> A: When you select an application to boot, the ESP32's partition table is rewritten to make it seem like the app is the only thing there. This firmware keeps track of what is installed and where. The app will then boot every time you power up the system, until you change it.

> **Q: Why rewrite the partition table when booting an application, why not keep all partitions of all apps?**
> 
> A: I tried. It causes conflicts with subtypes. Renumbering them mostly worked with single-partition applications. Multi-partition applications (like Go-Play) however mostly did not work because they (understandably) expect the types they declared when packaging.

> **Q: How can I get the boot menu without holding a button for 2 seconds?**
> 
> A: Unfortunately, I haven't found a reliable way of doing that yet


# Compilation
You can compile with the standard esp-idf with one caveat and it affects only the .fw installation method:
When you select an app to boot, you might be rebooted into the stock firmware where you need to press menu to actually boot your app.

Otherwise apply this patch to an otherwise unmodified esp-idf:
https://github.com/OtherCrashOverride/esp-idf/commit/49fbef73c300920d2f63c9afb705eefabe3dac87
