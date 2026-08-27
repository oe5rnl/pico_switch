# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/oe5rnl/0_dev/pico_switch/pico/switch_server/build/_deps/picotool-src/picoboot_flash_id"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/tmp"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/src/flash_id-stamp"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/src"
  "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/src/flash_id-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/src/flash_id-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/oe5rnl/0_dev/pico_switch/pico/picotool-usb-build/picoboot_flash_id/src/flash_id-stamp${cfgdir}") # cfgdir has leading slash
endif()
