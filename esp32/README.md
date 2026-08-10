# esp-switch

8-Kanal On/Off Switch Panel auf dem ESP32 Cheap Yellow Display (CYD,
ESP32-2432S028) mit LVGL 8 und TFT_eSPI.  
Das Touch-Display ist eine optionale, lokale Bedieneinheit zu pico_switch.


## Voraussetzungen

PlatformIO Core (`pio`) wird zum Bauen und Flashen benötigt.

### Installation (Debian/Ubuntu/Linux Mint)

Empfohlen über pipx (immer aktuell). Die apt-Version (4.3.x) ist mit
Python 3.12 nicht lauffähig (`AttributeError: ... 'resultcallback'`) und
sollte nicht verwendet werden.

```sh
sudo apt remove -y platformio   # falls die kaputte apt-Version installiert ist
sudo apt install -y pipx
pipx ensurepath
pipx install platformio
```

Neues Terminal öffnen (wegen `ensurepath`) und prüfen:

```sh
pio --version
```

## Build

```sh
pio run -e cyd          # ILI9341 Variante (Standard)
pio run -e cyd2usb      # ST7789 Variante
pio run -e cyd -t upload
```

## Upload-Skript

`upload.sh` baut und flasht die Firmware in einem Schritt (setzt den
pipx-`PATH`, findet `platformio.ini` selbst und wählt den Port automatisch —
bevorzugt `/dev/ttyUSB*`, nie den Pico-CDC `/dev/ttyACM*`):

```sh
./upload.sh              # Environment cyd, Port automatisch
./upload.sh -e cyd2usb   # anderes Environment (ST7789)
./upload.sh -p /dev/ttyUSB0   # Port erzwingen
./upload.sh -h           # Hilfe
```

Falls der Upload einen Boot-Modus-Fehler meldet, den ESP in den Download-Modus
bringen: **BOOT** halten, **RESET** kurz tippen, **BOOT** loslassen.

## UI

- 8 Buttons (4 × 2 Raster, Landscape 320×240), Titelzeile und IP-/Statuszeile.
- Beim Start steht `wait for init`, bis der Pico antwortet; danach werden Titel,
  Namen, Modus und Zustaende automatisch geladen.
- Relaismodus: Grau = OFF, Grün = ON, Rot = Rückmeldefehler; Beschriftung
  `Name` + Zustand. Antippen sendet `SWn:ON` / `SWn:OFF`.
- Szenenmodus (`MODE:SCENE`): die Buttons zeigen die aktivierten Szenennamen
  (blau), Grün = zuletzt aktiviert, Rot = Rückmeldefehler. Antippen sendet
  `SCENEn:GO`.
- Ein `|` in Namen erzeugt auf dem Display einen Zeilenumbruch.

## Pico-Link (UART, 115200 Baud)

Die serielle Schnittstelle (`GPIO1` = TX, `GPIO3` = RX) wird ausschließlich für
die Pico-Kommunikation genutzt (keine Debug-Ausgaben):

- Heartbeat `PING` alle 1000 ms; `pico_online` wird bei `PONG` gesetzt,
  Timeout nach 3000 ms führt zurück auf `wait for init`.
- Nach `pico_online` wird `GET DISPLAY` abgefragt und die UI aktualisiert.
- Empfangene Meldungen: `TITLE:`, `SUBTITLE:`, `NAMEn:`, `STATEn:ON/OFF`,
  `ERRORn:ON/OFF`, `SERRORn:ON/OFF`, `MODE:SCENE`/`MODE:RELAY`, `SCENEn:<name>`.

Nur die LVGL-Fonts montserrat 14/24/48 sind in `lv_conf.h` aktiviert.
