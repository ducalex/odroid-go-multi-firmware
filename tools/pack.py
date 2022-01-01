import subprocess
import datetime
import sys
import os

PROJECT_NAME = "odroid-go-multi-firmware"
PROJECT_VER = datetime.datetime.now().strftime("%Y%m%d")
PROJECT_BIN = "build/%s.bin" % PROJECT_NAME
PROJECT_IMG = "%s-%s.img" % (PROJECT_NAME, PROJECT_VER)

if not os.path.isfile(PROJECT_BIN):
    exit("You should run `idf.py .build` first!")

def readfile(filepath):
    with open(filepath, "rb") as f:
        return f.read()


print("Creating img file %s...\n" % PROJECT_IMG)

# TO DO: Read the partitions.csv instead...
with open(PROJECT_IMG, "wb") as fp:
    fp.write(b"\xFF" * 0x10000)
    fp.seek(0x1000)
    fp.write(readfile("build/bootloader/bootloader.bin"))
    fp.seek(0x8000)
    fp.write(readfile("build/partition_table/partition-table.bin"))
    fp.seek(0xF000)
    fp.write(readfile("build/phy_init_data.bin"))
    fp.seek(0x10000)
    fp.write(readfile(PROJECT_BIN))


print("Creating the .fw file...")

subprocess.run([
    sys.executable,
    "tools/mkfw.py",
    ("%s-%s.fw" % (PROJECT_NAME, PROJECT_VER)), "Multi-fw installer", PROJECT_BIN, # BIN as tile :)
    "1", "231", "0", "payload", PROJECT_IMG,  # Put payload first because we might overwrite that part during install
    "0", "0", "0", "installer", PROJECT_BIN,  #
], check=True)

print("all done!")
