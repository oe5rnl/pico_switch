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

# Nutzer-installierte Tools (pipx/pip --user, heruntergeladenes picotool) finden.
export PATH="$HOME/.local/bin:$PATH"

# ---- Konfiguration / Defaults ---------------------------------------------
PICOTOOL="${PICOTOOL:-picotool}"
ESP32_PORT="${ESP32_PORT:-}"
ESP32_BAUD="${ESP32_BAUD:-921600}"
# App-Image an 0x10000 (Bootloader/Partitionen liegen bereits auf dem ESP32).
# Fuer ein zusammengefuehrtes (merged) Image stattdessen --esp32-offset 0x0.
ESP32_OFFSET="${ESP32_OFFSET:-0x10000}"
# Fertige picotool-Binaries von Raspberry Pi (raspberrypi/pico-sdk-tools).
PICOTOOL_PREBUILT_URL="${PICOTOOL_PREBUILT_URL:-}"

WANT_PICO=0
WANT_ESP32=0
WANT_WIPE=0
PICO_IMAGE=""
ESP32_IMAGE=""
ASSUME_YES=0
EXPLICIT_TARGET=0

# XIP-Basis und Sektorgroesse des RP2350-Flash (fuer den Persistenz-Wipe).
PICO_XIP_BASE=$(( 0x10000000 ))
PICO_FLASH_SECTOR=$(( 0x1000 ))

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
  -w, --wipe-persist      Werksreset: gespeicherte Pico-Einstellungen (Persistenz)
                          per picotool loeschen. Ohne -p/-e allein nutzbar
                          (kein Image noetig); mit -p wird zuerst geloescht,
                          dann geflasht.
  (ohne -p/-e/-w: Auto - es wird geflasht, wozu ein Image gefunden/angegeben wird)

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
    -w|--wipe-persist) WANT_WIPE=1; EXPLICIT_TARGET=1; shift ;;
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
  # 1) Vorgefertigtes Binary von Raspberry Pi (kein root noetig, schnellster Weg)
  if install_picotool_prebuilt; then return 0; fi
  # 2) apt (falls die Distribution picotool als Paket kennt)
  if command -v apt-get >/dev/null 2>&1; then
    if sudo apt-get install -y picotool >/dev/null 2>&1; then
      hash -r
      if command -v picotool >/dev/null 2>&1; then echo "   via apt installiert."; return 0; fi
    fi
  fi
  # 3) Aus Quellcode bauen (Fallback)
  if install_picotool_from_source; then return 0; fi

  cat >&2 <<'EOF'
   Automatische Installation von picotool fehlgeschlagen.
   Manuell installieren (Beispiel):
     sudo apt-get install -y build-essential cmake libusb-1.0-0-dev pkg-config git
     git clone https://github.com/raspberrypi/picotool.git
     git clone https://github.com/raspberrypi/pico-sdk.git
     cd picotool && mkdir build && cd build
     PICO_SDK_PATH=../../pico-sdk cmake .. && make -j"$(nproc)" && sudo make install
EOF
  return 1
}

# picotool benoetigt libusb-1.0 zur Laufzeit; bei Bedarf per apt nachziehen.
ensure_libusb_runtime() {
  local ldc=""
  ldc="$(command -v ldconfig || true)"
  [[ -z "${ldc}" && -x /sbin/ldconfig ]] && ldc="/sbin/ldconfig"
  if [[ -n "${ldc}" ]] && "${ldc}" -p 2>/dev/null | grep -q "libusb-1.0\.so"; then
    return 0
  fi
  # Fallback: Bibliothek direkt in ueblichen Pfaden suchen (ohne ldconfig).
  if ls /lib/*/libusb-1.0.so.0 /usr/lib/*/libusb-1.0.so.0 >/dev/null 2>&1; then
    return 0
  fi
  if command -v apt-get >/dev/null 2>&1; then
    echo "   installiere Laufzeitbibliothek libusb-1.0 ..."
    sudo apt-get install -y libusb-1.0-0 >/dev/null 2>&1 || true
  fi
}

# Laedt ein passendes, vorgefertigtes picotool-Binary nach ~/.local/bin.
install_picotool_prebuilt() {
  local arch asset_arch
  arch="$(uname -m)"
  case "${arch}" in
    x86_64|amd64)  asset_arch="x86_64-lin" ;;
    aarch64|arm64) asset_arch="aarch64-lin" ;;
    *) echo "   kein vorgefertigtes picotool fuer Architektur ${arch}"; return 1 ;;
  esac
  command -v curl >/dev/null 2>&1 || { echo "   curl fehlt (fuer Binary-Download)"; return 1; }
  command -v tar  >/dev/null 2>&1 || { echo "   tar fehlt (fuer Binary-Entpacken)"; return 1; }
  ensure_libusb_runtime

  local url="${PICOTOOL_PREBUILT_URL}"
  if [[ -z "${url}" ]]; then
    # Neueste passende Asset-URL aus der GitHub-API holen ...
    url="$(curl -fsSL https://api.github.com/repos/raspberrypi/pico-sdk-tools/releases/latest 2>/dev/null \
          | grep -oE "https://[^\" ]*picotool-[^\" ]*-${asset_arch}\.tar\.gz" | head -1)" || true
  fi
  if [[ -z "${url}" ]]; then
    # ... sonst auf eine bekannte Version zurueckfallen.
    url="https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.0-1/picotool-2.3.0-${asset_arch}.tar.gz"
  fi

  echo "   lade vorgefertigtes picotool: ${url}"
  local tmp; tmp="$(mktemp -d)"
  if ! curl -fsSL "${url}" -o "${tmp}/picotool.tgz"; then
    echo "   Download fehlgeschlagen"; rm -rf "${tmp}"; return 1
  fi
  if ! tar xzf "${tmp}/picotool.tgz" -C "${tmp}"; then
    echo "   Entpacken fehlgeschlagen"; rm -rf "${tmp}"; return 1
  fi
  local bin; bin="$(find "${tmp}" -type f -name picotool | head -1)" || true
  if [[ -z "${bin}" ]]; then
    echo "   picotool im Archiv nicht gefunden"; rm -rf "${tmp}"; return 1
  fi
  mkdir -p "${HOME}/.local/bin"
  install -m 0755 "${bin}" "${HOME}/.local/bin/picotool"
  rm -rf "${tmp}"
  hash -r
  if command -v picotool >/dev/null 2>&1; then
    echo "   picotool nach ${HOME}/.local/bin installiert."
    return 0
  fi
  echo "   Hinweis: ~/.local/bin nicht im PATH?" >&2
  return 1
}

# Baut picotool aus dem Quellcode (benoetigt Pico-SDK) nach ~/.local/bin.
install_picotool_from_source() {
  echo "   versuche Build aus Quellcode ..."
  if ! (command -v git >/dev/null && command -v cmake >/dev/null && command -v make >/dev/null); then
    if command -v apt-get >/dev/null 2>&1; then
      sudo apt-get install -y build-essential cmake libusb-1.0-0-dev pkg-config git || return 1
    else
      echo "   Build-Werkzeuge (git/cmake/make) fehlen"; return 1
    fi
  fi
  ensure_libusb_runtime
  local tmp; tmp="$(mktemp -d)"
  if ! git clone --depth 1 https://github.com/raspberrypi/picotool.git "${tmp}/picotool"; then
    rm -rf "${tmp}"; return 1
  fi
  local sdk="${PICO_SDK_PATH:-}"
  if [[ -z "${sdk}" || ! -e "${sdk}/pico_sdk_init.cmake" ]]; then
    if ! git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git "${tmp}/pico-sdk"; then
      rm -rf "${tmp}"; return 1
    fi
    sdk="${tmp}/pico-sdk"
  fi
  if ! ( cd "${tmp}/picotool" && mkdir -p build && cd build \
         && PICO_SDK_PATH="${sdk}" cmake .. && make -j"$(nproc)" ); then
    rm -rf "${tmp}"; return 1
  fi
  mkdir -p "${HOME}/.local/bin"
  install -m 0755 "${tmp}/picotool/build/picotool" "${HOME}/.local/bin/picotool" || { rm -rf "${tmp}"; return 1; }
  rm -rf "${tmp}"
  hash -r
  command -v picotool >/dev/null 2>&1
}

# ---- Abhaengigkeiten pruefen ----------------------------------------------
declare -a MISSING_NAMES=()
declare -a MISSING_FUNCS=()

check_dependencies() {
  MISSING_NAMES=()
  MISSING_FUNCS=()
  if [[ ( "${WANT_PICO}" -eq 1 || "${WANT_WIPE}" -eq 1 ) ]] && ! have_picotool; then
    MISSING_NAMES+=("picotool (Pico-Flash/Werksreset ueber USB)")
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
[[ "${WANT_WIPE}"  -eq 1 ]] && echo "  Pico  : Werksreset (Persistenz loeschen)"
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
# Ermittelt die Flash-Groesse des verbundenen Pico in Bytes (Default 2 MiB).
# Erwartet den Pico im BOOTSEL-Modus (info -a liefert dort die Flash-Groesse).
pico_flash_size_bytes() {
  local out sizek
  out="$("${PICOTOOL}" info -a 2>/dev/null || true)"
  sizek="$(printf '%s\n' "${out}" | grep -oiE 'flash size:[[:space:]]*[0-9]+K' \
           | grep -oE '[0-9]+' | head -1)"
  if [[ -n "${sizek}" ]]; then
    echo $(( sizek * 1024 ))
  else
    echo $(( 2048 * 1024 ))
  fi
}

# Wartet, bis der Pico im BOOTSEL-Modus ansprechbar ist (Timeout in Sekunden).
wait_bootsel() {
  local timeout="${1:-10}" i=0
  while (( i < timeout * 5 )); do
    "${PICOTOOL}" info >/dev/null 2>&1 && return 0
    sleep 0.2; (( i++ )) || true
  done
  return 1
}

# Versetzt den laufenden Pico einmalig in den BOOTSEL-Modus und laesst ihn dort.
# So laufen anschliessende erase-Aufrufe ohne weitere Reboots (keine USB-Races).
enter_bootsel() {
  "${PICOTOOL}" info >/dev/null 2>&1 && return 0   # bereits in BOOTSEL
  echo "    versetze Pico in BOOTSEL-Modus ..."
  "${PICOTOOL}" reboot -f -u >/dev/null 2>&1 || true
  wait_bootsel 10 || die "Pico nicht im BOOTSEL-Modus erreichbar (USB-Verbindung pruefen)."
}

# Werksreset: loescht die Persistenz-Slots direkt per picotool.
# Die relativen Offsets spiegeln persist_flash_offsets() aus relay_server.cpp:
# 256K/512K/1024K/Flashende, jeweils minus ein Sektor (4 KiB).
# Der Pico wird EINMAL in BOOTSEL versetzt und bleibt dort (Caller startet neu).
wipe_pico_persist() {
  have_picotool || die "picotool fuer den Werksreset benoetigt."
  enter_bootsel
  local flash_bytes; flash_bytes="$(pico_flash_size_bytes)"
  local -a offsets=(
    $(( 256 * 1024  - PICO_FLASH_SECTOR ))
    $(( 512 * 1024  - PICO_FLASH_SECTOR ))
    $(( 1024 * 1024 - PICO_FLASH_SECTOR ))
    $(( flash_bytes - PICO_FLASH_SECTOR ))
  )
  echo "==> Pico-Werksreset: loesche Persistenz-Slots (Flash ${flash_bytes} Bytes)"
  local off from to
  local -a erased=()
  for off in "${offsets[@]}"; do
    (( off < 0 || off + PICO_FLASH_SECTOR > flash_bytes )) && continue
    # Doppelte Offsets (kleiner Flash) ueberspringen.
    local seen=0 e
    for e in "${erased[@]:-}"; do [[ "${e}" == "${off}" ]] && seen=1; done
    (( seen )) && continue
    erased+=("${off}")

    from=$(( PICO_XIP_BASE + off ))
    to=$(( from + PICO_FLASH_SECTOR ))
    printf '    Slot @ 0x%08X ... ' "${from}"
    # Ohne -f: Pico ist bereits in BOOTSEL und bleibt es (kein Reboot dazwischen).
    "${PICOTOOL}" erase -r "$(printf '0x%X' ${from})" "$(printf '0x%X' ${to})" >/dev/null
    echo "geloescht"
  done
  echo "    Persistenz geloescht. Naechster Start nutzt Defaults (admin / sw234)."
}

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
# Reihenfolge: erst Persistenz loeschen (falls -w), dann flashen.
if [[ "${WANT_WIPE}" -eq 1 ]]; then
  echo
  echo "!! WARNUNG: Werksreset loescht ALLE gespeicherten Pico-Einstellungen"
  echo "!!          (Namen, Szenen, Netzwerk, Benutzer/Passwoerter, ...)."
  if ask_yesno "Persistenz jetzt wirklich loeschen?"; then
    wipe_pico_persist
  else
    echo "Werksreset abgebrochen."
    WANT_WIPE=0
    [[ "${WANT_PICO}" -eq 0 && "${WANT_ESP32}" -eq 0 ]] && { echo; echo "Nichts zu tun."; exit 0; }
  fi
fi
if [[ "${WANT_PICO}"  -eq 1 ]]; then flash_pico;  fi
if [[ "${WANT_ESP32}" -eq 1 ]]; then flash_esp32; fi

# Standalone-Werksreset ohne Pico-Flash: den Pico aus BOOTSEL neu starten,
# damit die vorhandene Firmware mit Defaults weiterlaeuft.
if [[ "${WANT_WIPE}" -eq 1 && "${WANT_PICO}" -eq 0 ]]; then
  "${PICOTOOL}" reboot >/dev/null 2>&1 || true
fi

echo
echo "Fertig."
