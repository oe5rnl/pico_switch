# Relais-Webserver

Dieses Repository enthaelt zwei Module fuer eine 8-Kanal-Relaissteuerung:

| Verzeichnis | Zielplattform | Status |
|---|---|---|
| `esp32/` | ESP32-CYD mit LVGL-Touchpanel | lokales Schalt-Terminal (serieller Befehlssender) |
| `pico/switch_server/` | C++/Pico-SDK fuer W6300-EVB-PICO2 | native W6300-QSPI-Variante |

### Usermanual in der Datei: MANUAL.md

## System-Ueberblick: ESP32-Terminal + Pico-Implementierung

Die beiden Teile haben klar getrennte Rollen und werden als Frontend/Backend-Setup genutzt:

### esp32/ 
stellt das lokale Touch-Terminal bereit (8 Tasten, ON/OFF-Statusfarben, Szenen-Ansicht, Rückmeldefehler-Anzeige).
- Bei jeder Bedienung sendet das ESP32-Terminal einen zeilenbasierten Befehl (`SWn:ON`/`SWn:OFF` bzw. `SCENEn:GO`) über UART an den Pico.
- Im Szenenmodus erscheint auf der aktiven Szenen-Taste ein roter Punkt, sobald ein Relais direkt (nicht über eine Szene) geschaltet wurde. Der Punkt erlischt bei der nächsten Szenenaktivierung.

### pico/switch_server/
enthaelt die eigentliche Relais- und Netzwerk-Firmware und implementiert:  
- Einzelrelaisstuerung und Szenen
- Rückmeldeüberwachung: Prüfung ob Relais geschaltet haben
- HTTP-UI Werbserver,
- REST API-Server mit API Token
- Persistenz der Einstellungen 
- Authentifizierung und Benutzerverwaltung
- SSE: Automtisches Update der Webclients
- Versionsanzeige: Im Web-Footer werden beide Firmware-Versionen als `Firmware pico: xx.xxxxx.g<hash>  esp32: yy.yyyyy.g<hash>` angezeigt. Format: manuelle Hauptversion `xx`/`yy`, automatischer Git-Commit-Count `xxxxx`/`yyyyy` und der kurze Commit-Hash `g<hash>`. Zusaetzliche Marker: `-dirty` bei uncommittetem Stand, `+N` fuer N lokal noch nicht gepushte Commits (nur mit konfiguriertem Upstream). Der Hash macht den Stand eindeutig rueckverfolgbar. Hauptversion: Pico `FW_MAJOR` in `pico/switch_server/CMakeLists.txt`, ESP32 `ESP_FW_MAJOR` in `esp32/version.py`. Das ESP32 meldet seine Version per `VER:<version>` über UART an den Pico; ohne verbundenes Display steht dort `esp32: -`.
- Optionales ESP Touch Display über serielle Schnittstelle (esp_link Protokoll) schaltet die realen GPIO-Relais und meldet Titel, Namen, Modus und Zustände live an das Display zurück.

Typischer Zusammenspiel-Flow im Ueberblick:

1. Bediener tippt auf dem ESP32-Terminal einen Kanal oder eine Szene.
2. ESP32 erzeugt den seriellen Befehl (`SWn:ON/OFF` bzw. `SCENEn:GO`).
3. Der Pico verarbeitet den Befehl direkt über UART0 (`esp_link`).
4. Pico setzt den Relaiszustand, speichert ihn persistent und verteilt den neuen Zustand per API/SSE an Web-Clients sowie per UART zurück ans Display.

Damit bleibt die Verantwortung sauber getrennt: ESP32 = lokale Bedienoberflaeche, Pico = zentrale Steuer- und Netzwerkinstanz.

### Dual-Core-Architektur (RP2350): Relais schalten immer

Das Schalten der Relais muss **immer** funktionieren — unabhaengig vom LAN-Zustand
(kein Kabel, Kabel gezogen/wieder gesteckt, laufende DHCP-Suche, haengende TCP-
Verbindungen). Die blockierenden WIZnet-Aufrufe (`send`/`recv`/`sendto`/`disconnect`
warten per Busy-Loop) wuerden in einer einzigen Schleife das Relais- und Display-
Handling aushungern. Die Firmware nutzt deshalb **beide Kerne** des RP2350:

- **core0 (Steuerung):** ESP-UART (`esp_link`), Relais-GPIO, Rueckmeldungen, Szenen.
  Fasst **niemals** W6300 oder Flash an → kann nicht blockieren → Relais reagieren
  sofort, egal was das Netzwerk gerade macht.
- **core1 (Netzwerk):** W6300, HTTP-Server, DHCP, SSE-Live-Updates und das
  Flash-Schreiben. Darf blockieren, ohne core0 zu stoeren.

Synchronisation bewusst minimalistisch und deadlock-frei: ein einziger
`recursive_mutex_t` schuetzt den geteilten Zustand (nie ueber Netz-I/O oder Flash
gehalten), Auftraege laufen ueber wenige `volatile`-Flags zwischen den Kernen
(z. B. „SSE senden", „Flash speichern", „IP-Status ans Display"). Flash-Schreiben
laeuft nur auf core1 ueber `flash_safe_execute()`, waehrend core0 als
Lockout-Victim pausiert (`flash_range_erase` sperrt sonst beide Kerne). Details:
Abschnitt „2.7 Dual-Core-Architektur" in `CLAUDE.md`.


## Verkabelung zwischen Display und Pico

Die Kommunikation zwischen dem ESP32-Displayterminal und der Pico-Firmware laeuft ueber UART mit 115200 Baud.

GOIO Belegung:

- ESP32-Terminal: UART0 (`GPIO1` = TX, `GPIO3` = RX)
- Pico-Firmware: UART0 (`GP0` = TX, `GP1` = RX)

Verdrahtung (gekreuzt, TX auf RX):

| ESP32-CYD | Pico 2 (RP2350) | Zweck |
|---|---|---|
| GPIO1 (TX) | GP1 (RX) | ESP32 sendet Schaltbefehle zum Pico |
| GPIO3 (RX) | GP0 (TX) | Pico sendet Status/Antworten zum ESP32 |
| GND | GND | Gemeinsame Masse |

Wichtige Hinweise:

- Beide Boards arbeiten mit 3,3 V Logikpegeln (kein 5-V-UART anschliessen).
- TX und RX muessen immer gekreuzt verbunden sein.
- Die USB-Verbindungen der Boards bleiben zusaetzlich fuer Stromversorgung, Flashen und Debug moeglich.
- Verwendetes Textprotokoll auf der Leitung: z.B. `SW1:ON`, `SW1:OFF`, `SCENE1:GO`, `GET DISPLAY`, `VER:<version>`, `PING`/`PONG`. Das vollstaendige Protokoll ist in `MANUAL.md` und `CLAUDE.md` beschrieben.


## Kompilierung und Hochladen 

### Kurzanleitung für PICO und ESP32

Der PICO und das ESP32 cyd Display müssen getrennt geflashed werden. Jedes Modul hat seinen eigenen USB-C Anschluß.  
Beim Programmieren immer nur ein Modul anschiessen.  
Zum flasken des PICO muss dieser vorher bei gedrückter Taste (erste beim USB) eingeschaltet werden.  
Zum flashen des ESP32 muss die serielle Verbindung zwischen dem PICO (GP0 und GP1) und dem ESP32 getrennt werden.    
Beim ersten Aufruf der upload scripts werden einige Dateien aus dem Netz geladen.

```bash
cd esp32
./upload.sh

cd ..

cd pico
./upload.sh 

```
### Kompilierung und Hochladen im Detail (normalerweise nicht notwendig)


#### ESP32-Terminal (`esp32/`)

Kompilieren und hochladen über ein Helperscript

```bash
cd esp32
./upload.sh
```

Kompilieren (PlatformIO):

```bash
cd esp32
pio run -e cyd
```

Alternative Display-Variante:

```bash
pio run -e cyd2usb
```

Flashen auf das ESP32-Board:

```bash
pio run -e cyd -t upload
```

#### Pico-Implementierung (`pico/`)

Empfohlen: Helper-Skript `pico/upload.sh` (baut und kopiert die UF2 auf das
gemountete `RP2350`-Laufwerk):

```bash
cd pico
./upload.sh                    # Build + Upload aufs BOOTSEL-Laufwerk
./upload.sh -c                 # Clean-Build erzwingen
./upload.sh /pfad/zum/RP2350   # alternatives Ziel-Laufwerk
```

upload.sh klont beim ersten aufruf 

Zum Flashen muss der Pico im BOOTSEL-Modus sein:

1. `BOOTSEL` am Pico gedrueckt halten.
2. Pico per USB verbinden, dann `BOOTSEL` loslassen.
3. Das Skript kopiert die UF2 automatisch nach `/media/$USER/RP2350` (oder das
   uebergebene Ziel). Manuell kann die UF2 auch direkt aufs Laufwerk kopiert werden.

Alternativer manueller Build ohne Skript:

```bash
cd pico/switch_server
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Erzeugte UF2-Datei:

```text
pico/switch_server/build/switch_w6300_relay.uf2
```

CMake-Targets: `switch_w6300_relay` (Produktiv-Firmware) und
`uart_loopback_test` (UART0-Diagnose). Weitere Details: `pico/README.md`.

## Installation der benoetigten Tools

Die folgenden Schritte gelten fuer Debian/Ubuntu/Linux Mint.

### Pico (C++ / RP2350 + W6300)

ARM-Toolchain und Build-Werkzeuge installieren:

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  build-essential \
  git \
  gcc-arm-none-eabi \
  libnewlib-arm-none-eabi \
  libstdc++-arm-none-eabi-newlib \
  python3
```

Installation pruefen:

```bash
cmake --version
arm-none-eabi-gcc --version
```

WIZnet-PICO-C (Pico SDK, ioLibrary_Driver) inkl. Submodulen bereitstellen. Das
Skript `pico/upload.sh` klont den Code beim ersten Lauf automatisch nach
`pico/WIZnet-PICO-C` (per `.gitignore` ausgenommen). Manuell im
`pico/`-Verzeichnis. der Befehl muss nicht mauell ausgeführt werden.

```bash
git clone --recursive https://github.com/WIZnet-ioNIC/WIZnet-PICO-C.git WIZnet-PICO-C
cd WIZnet-PICO-C && git submodule update --init --recursive
```

Details und alternative Checkout-Pfade: `pico/README.md`.

> **WIZnet-Port bleibt unveraendert:** Der WIZnet-Beispiel-Port wartet in
> `wizchip_initialize()` von Haus aus endlos auf einen PHY-Link und wuerde damit
> ohne gestecktes LAN-Kabel den kompletten Boot (inkl. ESP-Display-Link)
> blockieren. Die Firmware ruft diese Funktion daher nicht auf, sondern nutzt eine
> eigene, nicht-blockierende Init (`wizchip_init_no_phy_wait()` in
> `relay_server.cpp`); der zeitbegrenzte Link-Check erfolgt anschliessend in
> `wait_for_phy_link()`. Der geklonte WIZnet-PICO-C-Code bleibt dadurch komplett
> unveraendert — kein Patch am Fremdcode, kein Zusatzskript.

### ESP32-CYD (PlatformIO)

PlatformIO Core (`pio`) wird zum Bauen und Flashen benoetigt. Empfohlen ueber
`pipx` (immer aktuell); die apt-Version (4.3.x) ist mit Python 3.12 nicht
lauffaehig und sollte nicht verwendet werden.

```bash
sudo apt remove -y platformio   # falls die kaputte apt-Version installiert ist
sudo apt install -y pipx
pipx ensurepath
pipx install platformio
```

Neues Terminal oeffnen (wegen `ensurepath`) und pruefen:

```bash
pio --version
```

Die Toolchains und Bibliotheken (LVGL, TFT_eSPI, XPT2046) laedt PlatformIO beim
ersten Build automatisch. Falls `pio` nicht gefunden wird:
`export PATH="$HOME/.local/bin:$PATH"`.

Projekt gebaut von OE5RNL, OE5NVL und Claude