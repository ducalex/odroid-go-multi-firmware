cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "components")

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GITREV
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(TIMESTAMP TODAY "%Y%m%d")

set(PROJECT_VER "${TODAY}-${GITREV}")

project(odroid-go-multi-firmware)
