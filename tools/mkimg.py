#!/usr/bin/env python
import sys, math, zlib, struct

def readfile(filepath):
    with open(filepath, "rb") as f: return f.read()

if len(sys.argv) < 4:
    exit("usage: mkimg.py image_filename offset binary [...]")

imageName = sys.argv[1]
imageData = bytearray(b"\xFF" * (16 * 1024 * 1024))
imageSize = 0
pos = 2

while pos < len(sys.argv):
    offset = int(sys.argv[pos+0], 0)
    fileName = sys.argv[pos+1]
    fileData = readfile(fileName)
    fileSize = len(fileData)
    pos += 2

    for x in range(0, fileSize):
        imageData[offset + x] = fileData[x]

    if (offset + fileSize) > imageSize:
        imageSize = offset + fileSize

    print("offset=%d, fileName='%s', fileSize=%d" % (offset, fileName, fileSize))

fp = open(imageName, "wb")
fp.write(imageData[:imageSize])
fp.close()

print("%s successfully created.\n" % imageName)

