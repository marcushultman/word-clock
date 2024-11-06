cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
  set(USERHOME $ENV{USERPROFILE})
else()
  set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.0.0)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if(EXISTS ${picoVscode})
  include(${picoVscode})
endif()

# ====================================================================================
set(PICO_BOARD adafruit_qtpy_rp2040 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(word_clock C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# RTC library
include(FetchContent)
FetchContent_Declare(
  pico_ds3231_repo
  GIT_REPOSITORY  https://github.com/alpertng02/pico-ds3231.git
  GIT_TAG         f090e052e3ac5a5373709ceba5e2b703856f4c78 # main (2024-10-21)
  SOURCE_SUBDIR   libraries/ds3231
)
FetchContent_MakeAvailable(pico_ds3231_repo)

# Fix missing dep in RTC library
target_link_libraries(pico_ds3231 pico_stdlib)

add_executable(word_clock
  word_clock.cpp
)

pico_enable_stdio_usb(word_clock 1)

# pull in common dependencies
target_link_libraries(word_clock
  pico_stdlib
  hardware_pio
  pico_ds3231
)

# create map/bin/hex file etc.
pico_add_extra_outputs(word_clock)

pico_generate_pio_header(
  word_clock
  ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
)
