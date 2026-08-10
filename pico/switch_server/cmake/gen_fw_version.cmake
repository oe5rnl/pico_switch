# Erzeugt fw_version.h mit "FW_MAJOR.<git-commit-count>" (5-stellig aufgefuellt).
find_package(Git QUIET)
set(COUNT 0)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
if(NOT COUNT MATCHES "^[0-9]+$")
  set(COUNT 0)
endif()

set(PADDED "${COUNT}")
string(LENGTH "${PADDED}" LEN)
while(LEN LESS 5)
  set(PADDED "0${PADDED}")
  string(LENGTH "${PADDED}" LEN)
endwhile()

set(CONTENT "#pragma once\n#define FW_VERSION \"${FW_MAJOR}.${PADDED}\"\n")
if(EXISTS "${HEADER_FILE}")
  file(READ "${HEADER_FILE}" OLD)
else()
  set(OLD "")
endif()
# Nur bei Aenderung schreiben, sonst unnoetige Rebuilds.
if(NOT OLD STREQUAL CONTENT)
  file(WRITE "${HEADER_FILE}" "${CONTENT}")
endif()
