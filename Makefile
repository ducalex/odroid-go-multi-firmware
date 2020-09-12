#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := odroid-go-multi-firmware
PROJECT_VER := $(shell date "+%Y%m%d")-$(shell git rev-parse --short HEAD)

include $(IDF_PATH)/make/project.mk

