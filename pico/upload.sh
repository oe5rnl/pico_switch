#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/switch_server"
BUILD_DIR="${PROJECT_DIR}/build"
UF2_FILE="${BUILD_DIR}/switch_w6300_relay.uf2"
LOCAL_COPY="${SCRIPT_DIR}/switch_w6300_relay_native.uf2"
WIZNET_PICO_C_PATH="${WIZNET_PICO_C_PATH:-${SCRIPT_DIR}/WIZnet-PICO-C}"
WIZNET_PICO_C_URL="${WIZNET_PICO_C_URL:-https://github.com/WIZnet-ioNIC/WIZnet-PICO-C.git}"
WIZNET_PICO_C_REF="${WIZNET_PICO_C_REF:-90136e8b522dd429f0fd966a6d30d8c95066c6e4}"
JOBS="${JOBS:-$(nproc)}"
INPUT_MODE="${INPUT_MODE:-taster}"
WIPE_PERSIST="${WIPE_PERSIST:-0}"
USB_FLASH="${USB_FLASH:-0}"
PICOTOOL="${PICOTOOL:-picotool}"

CLEAN=0
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--clean)
      CLEAN=1
      shift
      ;;
    -u|--usb)
      USB_FLASH=1
      shift
      ;;
    -m|--mode)
      INPUT_MODE="${2:-}"
      shift 2
      ;;
    -w|--wipe-persist)
      WIPE_PERSIST=1
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [-c|--clean] [-m|--mode taster|rueckm] [-w|--wipe-persist] [-u|--usb] [UPLOAD_DIR]

Options:
  -c, --clean          Build-Verzeichnis vor dem Bau loeschen (Clean Build)
  -m, --mode MODE      Eingangsmodus: taster (Default) oder rueckm
  -w, --wipe-persist   EINMAL-Werksreset: Firmware loescht beim Boot alle
                       gespeicherten Einstellungen (Persistenz-Slots). Danach
                       OHNE dieses Flag neu flashen, sonst wird jeder Boot geloescht.
  -u, --usb            Per picotool ueber USB flashen (ohne BOOTSEL-Taste). Der
                       laufende Pico wird per Software in BOOTSEL versetzt.
                       Ignoriert UPLOAD_DIR.
  -h, --help           Diese Hilfe anzeigen

UPLOAD_DIR  Zielverzeichnis fuer die UF2-Datei (Default: /media/\$USER/RP2350)

Umgebung:
  INPUT_MODE          Eingangsmodus (taster|rueckm), von -m/--mode ueberschrieben
  PICOTOOL            picotool-Binary fuer -u/--usb (Default: picotool aus PATH)
  WIZNET_PICO_C_PATH  Pfad zum WIZnet-PICO-C-Checkout (Default: ${SCRIPT_DIR}/WIZnet-PICO-C)
  WIZNET_PICO_C_URL   Git-URL fuer automatisches Klonen
  WIZNET_PICO_C_REF   Gepinnter Git-Commit/Tag (Default: 90136e8b522dd429f0fd966a6d30d8c95066c6e4)
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
    git clone "${WIZNET_PICO_C_URL}" "${WIZNET_PICO_C_PATH}"
  fi

  if [[ ! -d "${WIZNET_PICO_C_PATH}/.git" ]]; then
    echo "Fehler: ${WIZNET_PICO_C_PATH} ist kein Git-Checkout (Version nicht pinnbar)" >&2
    exit 1
  fi

  # Auf den gepinnten Commit bringen (falls noetig nachladen)
  if [[ "$(git -C "${WIZNET_PICO_C_PATH}" rev-parse HEAD)" != "${WIZNET_PICO_C_REF}" ]]; then
    echo "==> Setze WIZnet-PICO-C auf gepinnten Commit ${WIZNET_PICO_C_REF}"
    git -C "${WIZNET_PICO_C_PATH}" fetch origin "${WIZNET_PICO_C_REF}" 2>/dev/null \
      || git -C "${WIZNET_PICO_C_PATH}" fetch --all --tags
    git -C "${WIZNET_PICO_C_PATH}" checkout --detach "${WIZNET_PICO_C_REF}"
  fi

  # Submodule passend zum gepinnten Commit auschecken
  git -C "${WIZNET_PICO_C_PATH}" submodule update --init --recursive

  if [[ ! -f "${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake" ]]; then
    echo "Fehler: WIZnet-PICO-C nicht vollstaendig: ${WIZNET_PICO_C_PATH}" >&2
    echo "Erwartet wurde: ${WIZNET_PICO_C_PATH}/pico_sdk_import.cmake" >&2
    exit 1
  fi
}

ensure_wiznet_source

if [[ "${CLEAN}" -eq 1 ]]; then
  echo "==> Clean: entferne ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

if [[ "${INPUT_MODE}" != "taster" && "${INPUT_MODE}" != "rueckm" ]]; then
  echo "Fehler: ungueltiger INPUT_MODE '${INPUT_MODE}' (erlaubt: taster, rueckm)" >&2
  exit 1
fi

if [[ "${WIPE_PERSIST}" -eq 1 ]]; then
  PERSIST_WIPE_CMAKE=ON
  echo "!! WARNUNG: PERSIST_WIPE=ON -> diese Firmware LOESCHT beim Boot alle gespeicherten"
  echo "!!          Einstellungen. Danach OHNE -w/--wipe-persist neu flashen."
else
  PERSIST_WIPE_CMAKE=OFF
fi

echo "==> CMake configure (INPUT_MODE=${INPUT_MODE}, PERSIST_WIPE=${PERSIST_WIPE_CMAKE})"
cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DWIZNET_PICO_C_PATH="${WIZNET_PICO_C_PATH}" -DINPUT_MODE="${INPUT_MODE}" -DPERSIST_WIPE="${PERSIST_WIPE_CMAKE}"

echo "==> Make build (${JOBS} jobs)"
make -C "${BUILD_DIR}" -j"${JOBS}"

if [[ ! -f "${UF2_FILE}" ]]; then
  echo "Fehler: UF2-Datei nicht gefunden: ${UF2_FILE}" >&2
  exit 1
fi

echo "==> Lokale UF2-Kopie"
cp "${UF2_FILE}" "${LOCAL_COPY}"

if [[ "${USB_FLASH}" -eq 1 ]]; then
  if ! command -v "${PICOTOOL}" >/dev/null 2>&1; then
    echo "Fehler: picotool nicht gefunden (${PICOTOOL})." >&2
    echo "picotool mit USB-Support installieren oder PICOTOOL=... setzen." >&2
    exit 1
  fi
  echo "==> USB-Upload via picotool (ohne BOOTSEL-Taste)"
  "${PICOTOOL}" load -f -x "${UF2_FILE}"
  echo "Fertig."
  exit 0
fi

if [[ ! -d "${UPLOAD_DIR}" ]]; then
  echo "Fehler: Upload-Ziel nicht gefunden: ${UPLOAD_DIR}" >&2
  echo "Pico mit gedrueckter BOOTSEL-Taste anstecken, Ziel als Argument uebergeben oder -u/--usb nutzen." >&2
  exit 1
fi

echo "==> Upload nach ${UPLOAD_DIR}"
cp "${UF2_FILE}" "${UPLOAD_DIR}/"
sync

echo "Fertig."