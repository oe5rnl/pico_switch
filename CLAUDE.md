# Projekt: pico_switch

Das System besteht aus zwei Teilen: einem **Raspberry Pi Pico** (RP2350) und einem
**ESP32-CYD-Display**.

- Der **Pico** (Board `W6300-EVB-Pico2` mit LAN) realisiert die GPIO-/Relais-Steuerung
  und den Webserver.
- Das **ESP32-CYD-Display** dient zur Anzeige und Steuerung und ist über die serielle
  Schnittstelle (UART) angeschlossen.
- Beide Systeme kommunizieren über ein eigenes, zeilenbasiertes Textprotokoll.

---

## 1. Repository-Struktur

```
pico_switch/
├── CLAUDE.md                     # Diese Datei
├── README.md                     # Ausführliche Projekt-/Build-Doku
├── esp32/                        # ESP32-CYD-Displayterminal (PlatformIO)
│   ├── platformio.ini            # Envs: cyd, cyd2usb, loopback
│   ├── lv_conf.h                 # LVGL-Konfiguration (nur montserrat 14/24/48)
│   ├── upload.sh                 # Baut + flasht ESP32-Firmware (PlatformIO)
│   └── src/
│       ├── main.cpp              # Produktiv-Firmware (LVGL-UI + Pico-Link)
│       └── serial_loopback_test.cpp  # UART-Loopback-Diagnose (env loopback)
└── pico/
    ├── upload.sh                 # Baut + kopiert UF2 auf BOOTSEL-Laufwerk
    ├── switch_w6300_relay_native.uf2
    └── switch_server/            # Pico-Firmware (C++/Pico-SDK, CMake)
        ├── CMakeLists.txt    # Targets: switch_w6300_relay, uart_loopback_test
        ├── cmake/check_persist_overlap.cmake
        └── src/
            ├── relay_server.cpp       # Komplette Pico-Firmware
            └── uart_loopback_test.cpp # UART-Loopback-Diagnose mit Webseite
```

---

## 2. Pico-Firmware (`pico/switch_server/src/relay_server.cpp`)

Zentrale Backend-Firmware. Enthält Relais-Steuerung, HTTP-Server, Persistenz,
Authentifizierung, SSE-Live-Updates und den ESP-Link.

### 2.1 Konfiguration (`namespace cfg`)

| Konstante | Wert | Bedeutung |
|---|---|---|
| `RELAY_COUNT` | 8 | Anzahl der Relais/Kanäle |
| `SCENE_COUNT` | 8 | Anzahl konfigurierbarer Szenen |
| `RELAY_PINS` | `{2,3,4,5,6,7,8,9}` | GPIO-Pins der Relais |
| `FEEDBACK_PINS` | `{10,11,12,13,14,26,27,28}` | GPIO-Eingänge der physischen Rückmeldungen |
| `RELAY_ACTIVE_LOW` | `false` | Schaltlogik (HIGH = aktiv) |
| `DEFAULT_FEEDBACK_TIMEOUT_MS` | 500 | Standardfrist für eine Rückmeldung |
| `HTTP_PORT` | 80 | Webserver-Port |
| `HTTP_SOCKET_COUNT` | 8 | Parallele W6300-Sockets |
| `DHCP_SELECT_PIN` | 15 | HIGH/offen → DHCP, LOW → statische IP |
| `DHCP_SOCKET` | 0 | Socket für DHCP-Client |
| `SESSION_LIFETIME_MS` | 30 min | Session-Gültigkeit |
| `DEFAULT_ADMIN_USER` | `admin` | Standard-Benutzer |
| `DEFAULT_ADMIN_HASH` | SHA-256 von `sw234` | Standard-Passwort |

### 2.2 Netzwerk

- **W6300** über QSPI (Pins GP16–GP22), kein Konflikt mit UART0 (GP0/GP1).
- **Boot-Bootstrap** GP15: HIGH/offen → DHCP, LOW → statische IP.
- Statischer Fallback: `192.168.88.188/24`, GW `192.168.88.254`.
- Persistente statische IP/SN/GW in der Config speicherbar.

### 2.3 Persistenz (Flash)

- Struktur `PersistedConfig` (aktuell `PERSIST_VERSION = 7`), passt in einen Flash-Sektor.
- Versionierte Migration: `PersistedConfigV1` bis `V6` / aktuell.
- Gespeichert: Relais-Zustände, Namen, Titel/Untertitel, `public_access`,
  Benutzer, API-Keys, statische IP/SN/GW, Szenen-Modus + Szenen
  (Name, aktiv-Flag, je Kanal Aktion aus/ein/unverändert), Ausgangspolaritäten,
  Rückmeldeaktivierung/-polarität und gemeinsame Rückmeldezeit.
- CMake-Check `check_persist_overlap.cmake` stellt sicher, dass der Flash-Slot
  nicht mit dem Binär-Image kollidiert.

### 2.4 HTTP-Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/`, `/index.html` | Haupt-UI |
| GET/POST | `/login`, `/logout` | Authentifizierung |
| GET/POST | `/password` | Passwort ändern |
| GET/POST | `/config` | Titel/Namen/`public_access`, Ausgangspolarität (Low aktiv), Rückmeldung + Rückmeldezeit; Namensfelder zeigen die zugehörige Ausgangs-GPIO (z. B. „Relais 1 (GP2)") |
| GET/POST | `/network` | Statische IP-Einstellungen |
| GET/POST | `/admin` | Benutzer-/API-Key-Verwaltung |
| GET | `/me` | Aktueller Benutzer |
| GET | `/active_users` | Aktive Sessions/Gäste |
| GET | `/state` | Relais-Zustand (JSON, inkl. `scene_mode`) |
| GET/POST | `/scenes` | Szenen-Modus + Szenen konfigurieren (Admin) |
| GET | `/events` | Server-Sent Events (Live-Status) |
| POST | `/relay/<idx>/<on\|off\|toggle>` | Relais schalten |
| POST | `/scene/<idx>/activate` | Szene `idx` aktivieren |

- **Auth**: Session-Token (Cookie), Rollen, optionaler `public_access`, API-Keys.
- **SSE**: `/events` verteilt Zustandsänderungen live; Keepalive-`ping` alle 1 s.

### 2.5 Szenen-Modus

- Umschaltbar per Checkbox auf `/scenes` (Admin). Persistiert als `scene_mode`.
- Bei aktivem Szenen-Modus steuern die 8 Buttons (Web + ESP-Display) **nicht**
  direkt die Relais, sondern aktivieren je eine Szene.
- Eine **Szene** hat: `enabled`-Flag, Name und je Kanal eine Aktion:
  `0` = aus, `1` = ein, `2` = unverändert (Kanal wird nicht angetastet).
- Aktivierung ist **momentan** (kein gehaltener Aktiv-Zustand): `activate_scene()`
  wendet die Aktionen an, speichert bei Änderung und meldet den neuen Zustand.
- Direkte Relais-Steuerung (`/relay/...`) bleibt parallel nutzbar.

### 2.5 ESP-Link (`namespace esp_link`)

- UART0, `GP0` = TX, `GP1` = RX, 115200 Baud, 8N1, kein Flow-Control.
- `stdio` läuft **nur über USB** (`pico_enable_stdio_uart = 0`), daher stört
  `printf`-Debug die UART-Leitung nicht.
- `init()` konfiguriert die UART; `service()` verarbeitet eingehende Zeilen und
  sendet bei `*_dirty`-Flags Zustands-/Display-Updates; `flush_rx()` verwirft
  aufgelaufene Boot-Daten.

### 2.6 Startablauf (`main()`) — wichtig für Boot-Timing

Reihenfolge bewusst so gewählt, damit das Display **unabhängig vom DHCP** früh online geht:

1. `esp_link::init()` **als Erstes** → treibt `GP0` (TX) sofort auf UART-Idle (High),
   damit der ESP beim Kaltstart keine floatende/Break-Leitung sieht.
2. `stdio_init_all()`.
3. Relais- und Config-Init (`init_relays`, `load_config`, `init_users`, `apply_relay`).
   Ab hier stehen Titel/Namen/Zustände fest → Display kann bedient werden.
4. `esp_link::flush_rx()`, danach ~3 s Startpause, in der bereits `esp_link::service()`
   läuft (USB-Debug-Timing bleibt, ESP-Link ist aber sofort aktiv).
5. `init_network()` (DHCP/statisch). **Während der blockierenden DHCP-Warteschleife**
   in `acquire_dhcp_address()` wird `esp_link::service()` weiter aufgerufen.
6. Sockets öffnen, dann Endlos-Hauptschleife: `service_socket()`, `keepalive_sse()`,
   `esp_link::service()`.

---

## 3. ESP32-CYD-Display (`esp32/src/main.cpp`)

Lokales Touch-Terminal (Board ESP32-2432S028, „CYD"), LVGL 8 + TFT_eSPI + XPT2046-Touch.

- **UI**: 8 Buttons (4×2-Raster) mit ON/OFF-Statusfarben, Titelzeile.
  Startanzeige „wait for init", bis der Pico antwortet.
  Im **Szenen-Modus** zeigen die Buttons die Szenennamen (blau); nur aktivierte
  Szenen sind sichtbar, ein Tastendruck löst die Szene aus (momentan).
- **Pico-Link**: `PICO_UART = Serial` (UART0, `GPIO1` = TX, `GPIO3` = RX), 115200 Baud.
  Diese Schnittstelle wird **ausschließlich** für die Pico-Kommunikation genutzt —
  keine Debug-Ausgaben darüber.
- **Heartbeat**: sendet `PING` alle 1000 ms; `pico_online` wird bei `PONG` gesetzt,
  Timeout nach 3000 ms → zurück auf „wait for init".
- Nach `pico_online` wird `GET DISPLAY` abgefragt und die UI aktualisiert.
- `MODE:SCENE` / `MODE:RELAY` schaltet die UI live um; Szenennamen kommen per
  `SCENEn:<name>`. Tastendruck sendet `SWn:ON` / `SWn:OFF` bzw. `SCENEn:GO`.
- Nur die LVGL-Fonts montserrat 14/24/48 sind in `lv_conf.h` aktiviert.

---

## 4. Serielles Protokoll (UART, 115200 Baud, zeilenbasiert `\n`)

### ESP32 → Pico

| Befehl | Bedeutung |
|---|---|
| `PING` | Heartbeat / Verbindungscheck |
| `GET DISPLAY` | Vollständige Display-Konfiguration anfordern |
| `GET TITLE` | Nur Titel |
| `GET NAMES` | Nur Kanalnamen |
| `GET STATES` | Nur Zustände |
| `GET SUBTITLE` | Nur Untertitel |
| `SWn:ON` / `SWn:OFF` | Relais `n` (1–8) schalten |
| `SCENEn:GO` | Szene `n` (1–8) auslösen (Szenen-Modus) |

### Pico → ESP32

| Antwort | Bedeutung |
|---|---|
| `PONG` | Antwort auf `PING` |
| `TITLE:<text>` | Seitentitel |
| `SUBTITLE:<text>` | Untertitel |
| `NAMEn:<text>` | Name von Kanal `n` |
| `MODE:SCENE` / `MODE:RELAY` | Aktiver Modus (Szenen- oder Direktbetrieb) |
| `SCENEn:<text>` | Name von Szene `n` (leer = Szene inaktiv) |
| `STATEn:ON` / `STATEn:OFF` | Zustand von Kanal `n` |
| `ERRORn:ON` / `ERRORn:OFF` | Rückmeldefehler von Relais `n` |
| `SERRORn:ON` / `SERRORn:OFF` | Rückmeldefehler der zuletzt auslösenden Szene `n` |
| `END STATES` | Ende der Zustandsliste |
| `END NAMES` | Ende der Namensliste |
| `END DISPLAY` | Ende der vollständigen Display-Konfiguration |

---

## 5. Verkabelung Display ↔ Pico

UART, 115200 Baud, 3,3 V-Logik, gekreuzt (TX auf RX):

| ESP32-CYD | Pico 2 (RP2350) | Zweck |
|---|---|---|
| GPIO1 (TX) | GP1 (RX) | ESP32 → Pico (Schaltbefehle) |
| GPIO3 (RX) | GP0 (TX) | Pico → ESP32 (Status/Antworten) |
| GND | GND | Gemeinsame Masse (zwingend) |

Hinweise:

- **Keine 5-V-UART** anschließen (beide Boards 3,3 V).
- TX/RX immer gekreuzt.
- USB-Verbindungen bleiben zusätzlich für Strom, Flashen und Debug nutzbar.

---

## 6. Bauen & Flashen

### Pico (`pico/`)

```bash
cd pico
./upload.sh              # baut UF2 und kopiert es aufs BOOTSEL-Laufwerk
# ./upload.sh -c         # Clean-Build erzwingen
# ./upload.sh /pfad/zum/RP2350   # alternatives Ziel-Laufwerk
```

- Pico beim Flashen mit gedrückter **BOOTSEL**-Taste anstecken
  (Ziel-Laufwerk `/media/<user>/RP2350`).
- Targets: `switch_w6300_relay` (Produktiv), `uart_loopback_test` (Diagnose).
- Serielles Debug/CDC: `/dev/ttyACM0`, 115200.

### ESP32 (`esp32/`)

```bash
cd esp32
export PATH="$HOME/.local/bin:$PATH"     # PlatformIO via pipx
pio run -e cyd -t upload                 # Produktiv-Firmware (ILI9341)
pio run -e cyd2usb -t upload             # Variante ST7789
pio run -e loopback -t upload            # UART-Loopback-Test
```

- Upload-Port typischerweise `/dev/ttyUSB0` (CH340).
- Zum Flashen USB verbinden (CYD wird im Betrieb sonst extern versorgt).

---

## 7. Diagnose-Werkzeuge

- **Pico-Loopback** (`uart_loopback_test.cpp`): testet UART0 GP0↔GP1 (Jumper),
  Ergebnis als auto-refreshende Webseite (PASS/FAIL, Byte-Zähler, letzte TX/RX-Hex).
- **ESP-Loopback** (`serial_loopback_test.cpp`, env `loopback`): testet UART0
  (GPIO1↔GPIO3), Ergebnis auf dem TFT-Display (PASS grün / FAIL rot).

Beide Tests validieren jeweils die **eigene** UART-Hardware eines Boards.

---

## 8. Bekannte Fakten / Fallstricke

- Auf dem CYD sind `GPIO1`/`GPIO3` mit dem CH340-USB-Bridge geteilt.
- Der ESP-Boot-Log (ROM) wird über `GPIO1` ausgegeben; der Pico verwirft solche
  Fremdzeilen. Kritisch ist, dass der Pico-TX (`GP0`) beim Kaltstart früh auf
  Idle-High liegt (siehe Startablauf), sonst desynchronisiert der ESP-UART-Empfänger.
- Standard-Login: Benutzer `admin`, Passwort `sw234`.
- PlatformIO wird per `pipx` bereitgestellt; ggf. `export PATH="$HOME/.local/bin:$PATH"`.

