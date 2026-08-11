cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(PICO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(WIZNET_PICO_C_DEFAULT "${PICO_ROOT}/WIZnet-PICO-C")
set(WIZNET_PICO_C_PATH "${WIZNET_PICO_C_DEFAULT}" CACHE PATH "Path to WIZnet-PICO-C checkout")
if(NOT EXISTS "${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake")
    message(FATAL_ERROR "WIZnet-PICO-C nicht gefunden unter ${WIZNET_PICO_C_PATH}. build_upload.sh kann den Checkout automatisch dorthin laden, oder -DWIZNET_PICO_C_PATH=... setzen.")
endif()
set(BOARD_NAME W6300_EVB_PICO2)
set(PICO_PLATFORM rp2350)
set(WIZNET_CHIP W6300)
set(WIZCHIP_QSPI_MODE QSPI_QUAD_MODE)

add_definitions(-D_WIZCHIP_=W6300)
add_definitions(-DDEVICE_BOARD_NAME=W6300_EVB_PICO2)
add_definitions(-D_WIZCHIP_QSPI_MODE_=QSPI_QUAD_MODE)
add_definitions(-DPICO_USE_FASTEST_SUPPORTED_CLOCK=1)

set(PICO_SDK_PATH "${WIZNET_PICO_C_PATH}/libraries/pico-sdk" CACHE PATH "Pico SDK path")
include(${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake)

project(switch_w6300_relay C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

set(WIZNET_DIR "${WIZNET_PICO_C_PATH}/libraries/ioLibrary_Driver" CACHE PATH "WIZnet ioLibrary path")
set(PORT_DIR "${WIZNET_PICO_C_PATH}/port" CACHE PATH "WIZnet Pico port path")

add_compile_options(
    -Wall
    -Wno-format
    -Wno-unused-function
    -Wno-maybe-uninitialized
)

add_subdirectory(${WIZNET_PICO_C_PATH}/libraries ${CMAKE_BINARY_DIR}/wiznet_libraries)
add_subdirectory(${WIZNET_PICO_C_PATH}/port ${CMAKE_BINARY_DIR}/wiznet_port)

add_executable(switch_w6300_relay
    src/relay_server.cpp
)

target_include_directories(switch_w6300_relay PRIVATE
    ${WIZNET_DIR}/Ethernet
    ${WIZNET_DIR}/Ethernet/W6300
    ${PORT_DIR}
    ${PORT_DIR}/ioLibrary_Driver/inc
)

target_link_libraries(switch_w6300_relay PRIVATE
    pico_stdlib
    pico_multicore
    pico_flash
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_spi
    hardware_dma
    hardware_uart
    ETHERNET_FILES
    DHCP_FILES
    IOLIBRARY_FILES
)

# core0 (Steuerung) und core1 (Netzwerk) nutzen beide std::string -> malloc
# muss multicore-sicher sein (Spinlock um den Heap).
target_compile_definitions(switch_w6300_relay PRIVATE PICO_USE_MALLOC_MUTEX=1)

pico_enable_stdio_usb(switch_w6300_relay 1)
pico_enable_stdio_uart(switch_w6300_relay 0)
pico_add_extra_outputs(switch_w6300_relay)

# ---- Firmware-Version aus Git-Commit-Count (nur Web-Anzeige) --------------
# Erzeugt bei jedem Build generated/fw_version.h mit FW_VERSION="xx.xxxxx".
set(FW_MAJOR "05" CACHE STRING "Manuelle Hauptversion (xx)")
set(FW_VERSION_HEADER "${CMAKE_BINARY_DIR}/generated/fw_version.h")
add_custom_target(gen_fw_version
    BYPRODUCTS ${FW_VERSION_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DHEADER_FILE=${FW_VERSION_HEADER}
        -DFW_MAJOR=${FW_MAJOR}
        -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_fw_version.cmake
    VERBATIM
)
add_dependencies(switch_w6300_relay gen_fw_version)
target_include_directories(switch_w6300_relay PRIVATE ${CMAKE_BINARY_DIR}/generated)

# ---- Separates Test-Target: UART0-Loopback ueber Webseite ----------------
# Eigenstaendiges Programm, laesst die Originalfirmware unberuehrt.
# Bauen:   make -C build uart_loopback_test   (oder gesamter Build)
# Ergebnis: build/uart_loopback_test.uf2
add_executable(uart_loopback_test
    src/uart_loopback_test.cpp
)

target_include_directories(uart_loopback_test PRIVATE
    ${WIZNET_DIR}/Ethernet
    ${WIZNET_DIR}/Ethernet/W6300
    ${PORT_DIR}
    ${PORT_DIR}/ioLibrary_Driver/inc
)

target_link_libraries(uart_loopback_test PRIVATE
    pico_stdlib
    hardware_gpio
    hardware_uart
    ETHERNET_FILES
    DHCP_FILES
    IOLIBRARY_FILES
)

pico_enable_stdio_usb(uart_loopback_test 1)
pico_enable_stdio_uart(uart_loopback_test 0)
pico_add_extra_outputs(uart_loopback_test)

# Schutz: Firmware darf nicht in den ersten Persistenz-Slot (0x3f000) hineinwachsen.
# Sobald __flash_binary_end >= 0x1003f000 liegt, faellt der erste Slot weg und
# der Build bricht mit klarer Meldung ab. Wert spiegelt persist_flash_offsets()
# in src/relay_server.cpp.
set(PERSIST_FIRST_SLOT_ADDR "0x1003f000")
add_custom_command(TARGET switch_w6300_relay POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DELF_FILE=$<TARGET_FILE:switch_w6300_relay>
        -DNM=${CMAKE_NM}
        -DLIMIT=${PERSIST_FIRST_SLOT_ADDR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_persist_overlap.cmake
    VERBATIM
)
