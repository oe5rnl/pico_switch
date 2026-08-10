# Erzeugt fw_version.h mit "FW_MAJOR.<count>.g<hash>[-dirty][+N]" (count 5-stellig).
find_package(Git QUIET)
set(COUNT 0)
set(HASH "0000000")
set(DIRTY "")
set(AHEAD "")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  # Nur getrackte Aenderungen zaehlen als "dirty" (wie git describe --dirty).
  execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT GIT_STATUS STREQUAL "")
    set(DIRTY "-dirty")
  endif()
  # Anzahl noch nicht gepushter Commits (nur mit konfiguriertem Upstream).
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count "@{u}..HEAD"
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE AHEAD_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(AHEAD_COUNT MATCHES "^[0-9]+$" AND NOT AHEAD_COUNT STREQUAL "0")
    set(AHEAD "+${AHEAD_COUNT}")
  endif()
endif()
if(NOT COUNT MATCHES "^[0-9]+$")
  set(COUNT 0)
endif()
if(HASH STREQUAL "")
  set(HASH "0000000")
endif()

set(PADDED "${COUNT}")
string(LENGTH "${PADDED}" LEN)
while(LEN LESS 5)
  set(PADDED "0${PADDED}")
  string(LENGTH "${PADDED}" LEN)
endwhile()

set(CONTENT "#pragma once\n#define FW_VERSION \"${FW_MAJOR}.${PADDED}.g${HASH}${DIRTY}${AHEAD}\"\n")
if(EXISTS "${HEADER_FILE}")
  file(READ "${HEADER_FILE}" OLD)
else()
  set(OLD "")
endif()
# Nur bei Aenderung schreiben, sonst unnoetige Rebuilds.
if(NOT OLD STREQUAL CONTENT)
  file(WRITE "${HEADER_FILE}" "${CONTENT}")
endif()
