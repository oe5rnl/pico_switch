# Prueft nach dem Linken, ob die Firmware in den ersten Persistenz-Slot
# hineinwaechst. Aufruf: cmake -DELF_FILE=... -DNM=... -DLIMIT=0x1003f000 -P diese_datei.cmake

if(NOT ELF_FILE OR NOT EXISTS "${ELF_FILE}")
    message(FATAL_ERROR "ELF_FILE nicht gefunden: ${ELF_FILE}")
endif()
if(NOT NM)
    set(NM "arm-none-eabi-nm")
endif()
if(NOT LIMIT)
    message(FATAL_ERROR "LIMIT (Adresse erster Persistenz-Slot) nicht gesetzt")
endif()

execute_process(
    COMMAND ${NM} -n "${ELF_FILE}"
    OUTPUT_VARIABLE NM_OUTPUT
    RESULT_VARIABLE NM_RESULT
)
if(NOT NM_RESULT EQUAL 0)
    message(FATAL_ERROR "nm fehlgeschlagen: ${NM_OUTPUT}")
endif()

string(REGEX MATCH "([0-9a-fA-F]+) [^\n]* __flash_binary_end" _m "${NM_OUTPUT}")
if(NOT _m)
    message(FATAL_ERROR "__flash_binary_end nicht in ELF gefunden")
endif()
set(BIN_END_HEX "${CMAKE_MATCH_1}")
math(EXPR BIN_END "0x${BIN_END_HEX}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR LIMIT_DEC "${LIMIT}")
math(EXPR BIN_END_DEC "0x${BIN_END_HEX}")

if(BIN_END_DEC GREATER_EQUAL LIMIT_DEC)
    message(FATAL_ERROR
        "Firmware-Ende ${BIN_END} ueberschreitet den ersten Persistenz-Slot bei ${LIMIT}.\n"
        "Persistenz-Slot 0x3f000 muss in src/relay_server.cpp aus persist_flash_offsets() entfernt\n"
        "und durch einen weiter hinten liegenden Slot ersetzt werden, sonst werden Einstellungen\n"
        "beim Flashen geloescht."
    )
endif()
math(EXPR FREE "${LIMIT_DEC} - ${BIN_END_DEC}")
message(STATUS "Persist-Check: __flash_binary_end=${BIN_END}, freier Platz bis 1. Slot=${FREE} Bytes")
