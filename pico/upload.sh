#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/switch_server"
BUILD_DIR="${PROJECT_DIR}/build"
UF2_FILE="${BUILD_DIR}/switch_w6300_relay.uf2"
LOCAL_COPY="${SCRIPT_DIR}/switch_w6300_relay_native.uf2"
WIZNET_PICO_C_PATH="${WIZNET_PICO_C_PATH:-${SCRIPT_DIR}/WIZnet-PICO-C}"
WIZNET_PICO_C_URL="${WIZNET_PICO_C_URL:-https://github.com/WIZnet-ioNIC/WIZnet-PICO-C.git}"
JOBS="${JOBS:-$(nproc)}"

CLEAN=0
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--clean)
      CLEAN=1
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [-c|--clean] [UPLOAD_DIR]

Options:
  -c, --clean   Build-Verzeichnis vor dem Bau loeschen (Clean Build)
  -h, --help    Diese Hilfe anzeigen

UPLOAD_DIR  Zielverzeichnis fuer die UF2-Datei (Default: /media/\$USER/RP2350)

Umgebung:
  WIZNET_PICO_C_PATH  Pfad zum WIZnet-PICO-C-Checkout (Default: ${SCRIPT_DIR}/WIZnet-PICO-C)
  WIZNET_PICO_C_URL   Git-URL fuer automatisches Klonen
EOF
      exit 0
      ;;
    --)
      shift
      POSITIONAL+=("$@")
      break
      ;;
    -*)
      echo "Unbekannte Option: $1" >&2
      exit 1
      ;;
    *)
      POSITIONAL+=("$1")
      shift
      ;;
  esac
done

UPLOAD_DIR="${POSITIONAL[0]:-/media/${USER}/RP2350}"

ensure_wiznet_source() {
  if [[ ! -e "${WIZNET_PICO_C_PATH}" ]]; then
    echo "==> Lade WIZnet-PICO-C nach ${WIZNET_PICO_C_PATH}"
    mkdir -p "$(dirname "${WIZNET_PICO_C_PATH}")"
    git clone --recursive "${WIZNET_PICO_C_URL}" "${WIZNET_PICO_C_PATH}"
  fi

  if [[ ! -f "${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake" ]]; then
    echo "Fehler: WIZnet-PICO-C nicht vollstaendig: ${WIZNET_PICO_C_PATH}" >&2
    echo "Erwartet wurde: ${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake" >&2
    exit 1
  fi

  if [[ -d "${WIZNET_PICO_C_PATH}/.git" && ! -f "${WIZNET_PICO_C_PATH}/libraries/pico-sdk/CMakeLists.txt" ]]; then
    echo "==> Aktualisiere WIZnet-PICO-C-Submodule"
    git -C "${WIZNET_PICO_C_PATH}" submodule update --init --recursive
  fi
}

ensure_wiznet_source

if [[ "${CLEAN}" -eq 1 ]]; then
  echo "==> Clean: entferne ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

echo "==> CMake configure"
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DWIZNET_PICO_C_PATH="${WIZNET_PICO_C_PATH}"

echo "==> Make build (${JOBS} jobs)"
make -C "${BUILD_DIR}" -j"${JOBS}"

if [[ ! -f "${UF2_FILE}" ]]; then
  echo "Fehler: UF2-Datei nicht gefunden: ${UF2_FILE}" >&2
  exit 1
fi

echo "==> Lokale UF2-Kopie"
cp "${UF2_FILE}" "${LOCAL_COPY}"

if [[ ! -d "${UPLOAD_DIR}" ]]; then
  echo "Fehler: Upload-Ziel nicht gefunden: ${UPLOAD_DIR}" >&2
  echo "Pico mit gedrueckter BOOTSEL-Taste anstecken oder Ziel als Argument uebergeben." >&2
  exit 1
fi

echo "==> Upload nach ${UPLOAD_DIR}"
cp "${UF2_FILE}" "${UPLOAD_DIR}/"
sync

echo "Fertig."