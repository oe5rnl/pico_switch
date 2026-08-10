#!/usr/bin/env bash
set -euo pipefail

# Baut und flasht die ESP32-CYD-Firmware via PlatformIO.
# Aufruf aus beliebigem Verzeichnis moeglich.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"   # esp32/ (enthaelt platformio.ini)

ENVIRONMENT="${ENVIRONMENT:-cyd}"
UPLOAD_PORT="${UPLOAD_PORT:-}"

# PlatformIO wird per pipx bereitgestellt.
export PATH="$HOME/.local/bin:$PATH"

usage() {
  cat <<EOF
Usage: $(basename "$0") [-e ENV] [-p PORT] [-h]

Options:
  -e ENV    PlatformIO-Environment (Default: cyd; z.B. cyd2usb)
  -p PORT   Upload-Port erzwingen (Default: Auto, sonst /dev/ttyUSB0)
  -h        Diese Hilfe anzeigen
EOF
}

while getopts ":e:p:h" opt; do
  case "$opt" in
    e) ENVIRONMENT="$OPTARG" ;;
    p) UPLOAD_PORT="$OPTARG" ;;
    h) usage; exit 0 ;;
    \?) echo "Unbekannte Option: -$OPTARG" >&2; usage; exit 1 ;;
    :) echo "Option -$OPTARG benoetigt ein Argument." >&2; exit 1 ;;
  esac
done

if ! command -v pio >/dev/null 2>&1; then
  echo "Fehler: 'pio' nicht gefunden. PlatformIO via pipx installieren." >&2
  exit 1
fi

# Auto-Portwahl: bevorzugt CH340 (/dev/ttyUSB*), nie den Pico-CDC (/dev/ttyACM*).
if [[ -z "${UPLOAD_PORT}" ]]; then
  for p in /dev/ttyUSB0 /dev/ttyUSB1; do
    if [[ -e "$p" ]]; then UPLOAD_PORT="$p"; break; fi
  done
fi

echo "==> Environment: ${ENVIRONMENT}"
if [[ -n "${UPLOAD_PORT}" ]]; then
  echo "==> Upload-Port: ${UPLOAD_PORT}"
  pio run -d "${PROJECT_DIR}" -e "${ENVIRONMENT}" -t upload --upload-port "${UPLOAD_PORT}"
else
  echo "==> Upload-Port: Auto (PlatformIO waehlt selbst)"
  pio run -d "${PROJECT_DIR}" -e "${ENVIRONMENT}" -t upload
fi

echo "Fertig."

