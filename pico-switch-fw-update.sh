#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# pico-switch-fw-update.sh
#
# Flasht vorgefertigte Firmware-Images auf den Pico (RP2350) und/oder das
# ESP32-CYD-Display OHNE BOOTSEL-Taste / ohne manuelles Umstecken:
#   * Pico  : per picotool ueber USB (Software-Reboot in BOOTSEL)
#   * ESP32 : per esptool ueber die serielle USB-Bruecke (Auto-Reset)
#
# Die FW-Images muessen im aktuellen Verzeichnis liegen oder werden per
# -i / --image (bzw. --pico-image/--esp32-image) angegeben:
#   *.uf2 -> Pico-Firmware       *.bin -> ESP32-Firmware
#
# Vor dem Flashen prueft das Skript, ob alle noetigen Programme installiert
# sind. Fehlt etwas, werden die fehlenden Komponenten aufgelistet und es wird
# gefragt, ob sie installiert werden sollen. Nach erfolgreicher Installation
# wird gefragt, ob der Upload durchgefuehrt werden soll.
# ---------------------------------------------------------------------------

SELF="$(basename "$0")"

# ---- Konfiguration / Defaults ---------------------------------------------
PICOTOOL="${PICOTOOL:-picotool}"
ESP32_PORT="${ESP32_PORT:-}"
ESP32_BAUD="${ESP32_BAUD:-921600}"
# App-Image an 0x10000 (Bootloader/Partitionen liegen bereits auf dem ESP32).
# Fuer ein zusammengefuehrtes (merged) Image stattdessen --esp32-offset 0x0.
ESP32_OFFSET="${ESP32_OFFSET:-0x10000}"

WANT_PICO=0
WANT_ESP32=0
PICO_IMAGE=""
ESP32_IMAGE=""
ASSUME_YES=0
EXPLICIT_TARGET=0

# ESP-Kommando (wird bei der Tool-Pruefung gesetzt)
ESPTOOL_CMD=()

usage() {
  cat <<EOF
Usage: ${SELF} [OPTIONEN]

Flasht vorgefertigte Firmware ohne BOOTSEL-Taste:
  Pico  -> picotool (USB),  ESP32 -> esptool (serielle USB-Bruecke)

Zielauswahl:
  -p, --pico              Pico-Firmware flashen
  -e, --esp32             ESP32-Firmware flashen
  (ohne -p/-e: Auto - es wird geflasht, wozu ein Image gefunden/angegeben wird)

Images (sonst im aktuellen Verzeichnis gesucht):
  -i, --image PATH        FW-Image; Typ nach Endung (.uf2=Pico, .bin=ESP32),
                          aktiviert das passende Ziel. Mehrfach nutzbar.
      --pico-image PATH   explizites Pico-Image (.uf2)
      --esp32-image PATH  explizites ESP32-Image (.bin)

ESP32-Optionen:
      --port PORT         serieller Port (Default: auto /dev/ttyUSB0)
      --esp32-offset OFF  Flash-Offset (Default ${ESP32_OFFSET}; 0x0 fuer merged image)

Sonstiges:
  -y, --yes               alle Rueckfragen automatisch mit "ja" beantworten
  -h, --help              diese Hilfe anzeigen

Umgebungsvariablen:
  PICOTOOL, ESP32_PORT, ESP32_BAUD (${ESP32_BAUD}), ESP32_OFFSET (${ESP32_OFFSET})
EOF
}

die() { echo "Fehler: $*" >&2; exit 1; }

# Ja/Nein-Frage (respektiert -y). Default = nein.
ask_yesno() {
  local prompt="$1"
  if [[ "${ASSUME_YES}" -eq 1 ]]; then
    echo "${prompt} [J/n] j (automatisch)"
    return 0
  fi
  local ans=""
  read -r -p "${prompt} [j/N] " ans || true
  [[ "${ans}" =~ ^([jJ][aA]?|[yY][eE]?[sS]?)$ ]]
}

# ---- Argumente parsen -----------------------------------------------------
add_image_by_ext() {
  local path="$1"
  case "${path,,}" in
    *.uf2) PICO_IMAGE="${path}"; WANT_PICO=1; EXPLICIT_TARGET=1 ;;
    *.bin) ESP32_IMAGE="${path}"; WANT_ESP32=1; EXPLICIT_TARGET=1 ;;
    *) die "unbekannter Image-Typ (nur .uf2 oder .bin): ${path}" ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--pico)  WANT_PICO=1; EXPLICIT_TARGET=1; shift ;;
    -e|--esp32) WANT_ESP32=1; EXPLICIT_TARGET=1; shift ;;
    -i|--image) [[ $# -ge 2 ]] || die "$1 benoetigt ein Argument"; add_image_by_ext "$2"; shift 2 ;;
    --pico-image)  [[ $# -ge 2 ]] || die "$1 benoetigt ein Argument"; PICO_IMAGE="$2"; WANT_PICO=1; EXPLICIT_TARGET=1; shift 2 ;;
    --esp32-image) [[ $# -ge 2 ]] || die "$1 benoetigt ein Argument"; ESP32_IMAGE="$2"; WANT_ESP32=1; EXPLICIT_TARGET=1; shift 2 ;;
    --port)         [[ $# -ge 2 ]] || die "$1 benoetigt ein Argument"; ESP32_PORT="$2"; shift 2 ;;
    --esp32-offset) [[ $# -ge 2 ]] || die "$1 benoetigt ein Argument"; ESP32_OFFSET="$2"; shift 2 ;;
    -y|--yes) ASSUME_YES=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; break ;;
    -*) die "unbekannte Option: $1 (siehe -h)" ;;
    *)  add_image_by_ext "$1"; shift ;;
  esac
done

# ---- Image-Aufloesung im aktuellen Verzeichnis ----------------------------
# Sucht genau ein Image mit passender Endung; bei 0 oder >1 klare Meldung.
find_single_image() {
  local ext="$1"; shift
  local -a matches=()
  local f
  shopt -s nullglob
  for f in ./*."${ext}"; do matches+=("${f#./}"); done
  shopt -u nullglob
  if [[ ${#matches[@]} -eq 1 ]]; then
    printf '%s\n' "${matches[0]}"
    return 0
  fi
  if [[ ${#matches[@]} -eq 0 ]]; then
    return 1
  fi
  echo "__MULTI__:${matches[*]}"
  return 2
}

# Auto-Modus: kein Ziel explizit gewaehlt -> anhand vorhandener Images ableiten
if [[ "${EXPLICIT_TARGET}" -eq 0 ]]; then
  if img="$(find_single_image uf2)"; then PICO_IMAGE="${img}"; WANT_PICO=1; fi
  if img="$(find_single_image bin)"; then ESP32_IMAGE="${img}"; WANT_ESP32=1; fi
  if [[ "${WANT_PICO}" -eq 0 && "${WANT_ESP32}" -eq 0 ]]; then
    echo "Kein Firmware-Image im aktuellen Verzeichnis gefunden (*.uf2 / *.bin)." >&2
    echo "Image per -i PATH angeben oder in dieses Verzeichnis legen." >&2
    echo >&2
    usage
    exit 1
  fi
fi

# Fehlende Images fuer gewaehlte Ziele nachziehen (aus cwd)
resolve_image() {
  local ext="$1" curval="$2" label="$3"
  if [[ -n "${curval}" ]]; then printf '%s\n' "${curval}"; return 0; fi
  local res
  if res="$(find_single_image "${ext}")"; then
    printf '%s\n' "${res}"; return 0
  elif [[ "${res:-}" == __MULTI__:* ]]; then
    die "mehrere ${label}-Images (*.${ext}) gefunden: ${res#__MULTI__:} - bitte per Option angeben"
  else
    die "kein ${label}-Image (*.${ext}) gefunden - per Option angeben"
  fi
}

if [[ "${WANT_PICO}" -eq 1 ]]; then
  PICO_IMAGE="$(resolve_image uf2 "${PICO_IMAGE}" "Pico")"
  [[ -f "${PICO_IMAGE}" ]] || die "Pico-Image nicht gefunden: ${PICO_IMAGE}"
fi
if [[ "${WANT_ESP32}" -eq 1 ]]; then
  ESP32_IMAGE="$(resolve_image bin "${ESP32_IMAGE}" "ESP32")"
  [[ -f "${ESP32_IMAGE}" ]] || die "ESP32-Image nicht gefunden: ${ESP32_IMAGE}"
fi

# ---- Tool-Erkennung -------------------------------------------------------
detect_esptool() {
  export PATH="$HOME/.local/bin:$PATH"
  # 'esptool' bevorzugen (nicht deprecated); Fallbacks fuer aeltere Setups.
  if command -v esptool    >/dev/null 2>&1; then ESPTOOL_CMD=(esptool);    return 0; fi
  if command -v esptool.py >/dev/null 2>&1; then ESPTOOL_CMD=(esptool.py); return 0; fi
  if python3 -m esptool version >/dev/null 2>&1; then ESPTOOL_CMD=(python3 -m esptool); return 0; fi
  ESPTOOL_CMD=()
  return 1
}

have_picotool() { command -v "${PICOTOOL}" >/dev/null 2>&1; }

# ---- Installations-Routinen -----------------------------------------------
install_esptool() {
  echo "==> Installiere esptool ..."
  if command -v pipx >/dev/null 2>&1; then
    pipx install esptool && return 0
    echo "   pipx fehlgeschlagen, versuche pip --user ..."
  fi
  if command -v pip3 >/dev/null 2>&1; then
    pip3 install --user esptool && return 0
  elif command -v pip >/dev/null 2>&1; then
    pip install --user esptool && return 0
  fi
  if command -v apt-get >/dev/null 2>&1; then
    echo "   versuche apt-get install esptool ..."
    sudo apt-get update && sudo apt-get install -y esptool && return 0
  fi
  return 1
}

install_picotool() {
  echo "==> Installiere picotool ..."
  if command -v apt-get >/dev/null 2>&1; then
    if sudo apt-get update && sudo apt-get install -y picotool; then
      return 0
    fi
  fi
  cat >&2 <<'EOF'
   Automatische Installation von picotool nicht moeglich.
   picotool ist in dieser Distribution nicht als Paket verfuegbar.
   Manuell installieren (Beispiel):
     sudo apt-get install -y build-essential cmake libusb-1.0-0-dev pkg-config
     git clone https://github.com/raspberrypi/picotool.git
     cd picotool && mkdir build && cd build
     cmake .. && make -j"$(nproc)" && sudo make install
EOF
  return 1
}

# ---- Abhaengigkeiten pruefen ----------------------------------------------
declare -a MISSING_NAMES=()
declare -a MISSING_FUNCS=()

check_dependencies() {
  MISSING_NAMES=()
  MISSING_FUNCS=()
  if [[ "${WANT_PICO}" -eq 1 ]] && ! have_picotool; then
    MISSING_NAMES+=("picotool (Pico-Flash ueber USB)")
    MISSING_FUNCS+=("install_picotool")
  fi
  if [[ "${WANT_ESP32}" -eq 1 ]] && ! detect_esptool; then
    MISSING_NAMES+=("esptool (ESP32-Flash ueber Serial)")
    MISSING_FUNCS+=("install_esptool")
  fi
}

echo "== pico-switch Firmware-Update =="
[[ "${WANT_PICO}"  -eq 1 ]] && echo "  Pico  : ${PICO_IMAGE}"
[[ "${WANT_ESP32}" -eq 1 ]] && echo "  ESP32 : ${ESP32_IMAGE} (offset ${ESP32_OFFSET})"
echo

check_dependencies

INSTALLED_SOMETHING=0
if [[ ${#MISSING_NAMES[@]} -gt 0 ]]; then
  echo "Es fehlen folgende Komponenten fuer den Upload:"
  for n in "${MISSING_NAMES[@]}"; do echo "  - ${n}"; done
  echo
  if ask_yesno "Fehlende Komponenten jetzt installieren?"; then
    for fn in "${MISSING_FUNCS[@]}"; do
      if "${fn}"; then INSTALLED_SOMETHING=1; else die "Installation fehlgeschlagen (${fn})."; fi
    done
    echo
    # Erneut pruefen, damit die frisch installierten Tools erkannt werden.
    check_dependencies
    if [[ ${#MISSING_NAMES[@]} -gt 0 ]]; then
      echo "Weiterhin nicht verfuegbar:" >&2
      for n in "${MISSING_NAMES[@]}"; do echo "  - ${n}" >&2; done
      die "Upload nicht moeglich."
    fi
    echo "Alle benoetigten Komponenten sind jetzt installiert."
  else
    die "Ohne die fehlenden Komponenten ist kein Upload moeglich."
  fi
fi

# Nach einer Installation ausdruecklich fragen, ob geflasht werden soll.
if [[ "${INSTALLED_SOMETHING}" -eq 1 ]]; then
  echo
  if ! ask_yesno "Firmware jetzt flashen?"; then
    echo "Abgebrochen. Es wurde nichts geflasht."
    exit 0
  fi
fi

# ---- Flash-Routinen -------------------------------------------------------
flash_pico() {
  echo "==> Pico flashen (ohne BOOTSEL) via picotool: ${PICO_IMAGE}"
  # -f: laufenden Pico per Software in BOOTSEL versetzen; -x: danach starten
  "${PICOTOOL}" load -f -x "${PICO_IMAGE}"
  echo "    Pico-Upload fertig."
}

flash_esp32() {
  detect_esptool || die "esptool nicht gefunden."
  local port="${ESP32_PORT}"
  if [[ -z "${port}" ]]; then
    for p in /dev/ttyUSB0 /dev/ttyUSB1; do
      if [[ -e "$p" ]]; then port="$p"; break; fi
    done
  fi
  echo "==> ESP32 flashen via ${ESPTOOL_CMD[*]}: ${ESP32_IMAGE} @ ${ESP32_OFFSET}"
  local -a cmd=("${ESPTOOL_CMD[@]}" --chip esp32 --baud "${ESP32_BAUD}")
  if [[ -n "${port}" ]]; then
    cmd+=(--port "${port}")
    echo "    Port: ${port}"
  else
    echo "    Port: Auto (esptool waehlt selbst)"
  fi
  cmd+=(write_flash "${ESP32_OFFSET}" "${ESP32_IMAGE}")
  "${cmd[@]}"
  echo "    ESP32-Upload fertig."
}

# ---- Upload ausfuehren ----------------------------------------------------
if [[ "${WANT_PICO}"  -eq 1 ]]; then flash_pico;  fi
if [[ "${WANT_ESP32}" -eq 1 ]]; then flash_esp32; fi

echo
echo "Fertig."
