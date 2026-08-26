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
| `FEEDBACK_PINS` | `{10,11,12,13,14,26,27,28}` | GPIO-Eingänge: Rückmeldungen **oder** Taster (je nach `INPUT_MODE`, siehe 2.9) |
| `RELAY_ACTIVE_LOW` | `false` | Schaltlogik (HIGH = aktiv) |
| `DEFAULT_FEEDBACK_TIMEOUT_MS` | 500 | Standardfrist für eine Rückmeldung (Modus `rueckm`) |
| `DEFAULT_INPUT_TIME_MS` | 25 (Taster) / 500 (Rückm.) | Default für Entprell-/Rückmeldezeit je Eingangsmodus |
| `DEFAULT_IMPULSE_MS` | 300 | Standard-Impulszeit je Ausgang (Impuls-Modus) |
| `MIN/MAX_IMPULSE_MS` | 100 / 2000 | Grenzen der je Ausgang einstellbaren Impulszeit |
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

- Struktur `PersistedConfig` (aktuell `PERSIST_VERSION = 8`), passt in einen Flash-Sektor.
- Versionierte Migration: `PersistedConfigV1` bis `V7` / aktuell.
- Gespeichert: Relais-Zustände, Namen, Titel/Untertitel, `public_access`,
  Benutzer, API-Keys, statische IP/SN/GW, Szenen-Modus + Szenen
  (Name, aktiv-Flag, je Kanal Aktion aus/ein/unverändert), Ausgangspolaritäten,
  Rückmeldeaktivierung/-polarität, gemeinsame Rückmeldezeit sowie je Ausgang
  Impuls-Aktivierung und Impulszeit (100–2000 ms).
- CMake-Check `check_persist_overlap.cmake` stellt sicher, dass der Flash-Slot
  nicht mit dem Binär-Image kollidiert.

### 2.4 HTTP-Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/`, `/index.html` | Haupt-UI |
| GET/POST | `/login`, `/logout` | Authentifizierung |
| GET/POST | `/password` | Passwort ändern |
| GET/POST | `/config` | Titel/Namen/`public_access`, Ausgangspolarität (Low aktiv), je Ausgang Impuls (Checkbox + Impulszeit 100–2000 ms, Default 300), Rückmeldung + Rückmeldezeit; Namensfelder zeigen die zugehörige Ausgangs-GPIO (z. B. „Relais 1 (GP2)"). Im Taster-Modus (2.9) entfallen die Felder „Rückmeldung"/„Rückm. LOW"; stattdessen erscheinen „Taster GPxx" + Pseudo-LED und „Taster-Entprellzeit" |
| GET/POST | `/network` | Statische IP-Einstellungen |
| GET/POST | `/admin` | Benutzer-/API-Key-Verwaltung |
| GET | `/me` | Aktueller Benutzer |
| GET | `/active_users` | Aktive Sessions/Gäste |
| GET | `/state` | Relais-Zustand (JSON, inkl. `scene_mode`; im Taster-Modus zusätzlich `buttons`) |
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

### 2.6 Versionsanzeige

Im Web-Footer werden beide Firmware-Versionen angezeigt:
`Firmware pico: xx.xxxxx.g<hash>  esp32: yy.yyyyy.g<hash>`.

- Format: manuelle Hauptversion `xx`/`yy`, automatischer Git-Commit-Count
  `xxxxx`/`yyyyy` (5-stellig) und kurzer Commit-Hash `g<hash>`.
- Marker: `-dirty` bei uncommittetem (getracktem) Stand, `+N` für N lokal noch
  nicht gepushte Commits (nur mit konfiguriertem Upstream).
- Hauptversion einstellen: Pico `FW_MAJOR` in
  `pico/switch_server/CMakeLists.txt`, ESP32 `ESP_FW_MAJOR` in `esp32/version.py`.
- Erzeugung: Pico generiert bei jedem Build `build/generated/fw_version.h` per
  `cmake/gen_fw_version.cmake`; ESP32 injiziert `ESP_FW_VERSION` per
  PlatformIO-Pre-Skript `esp32/version.py`.
- Das ESP32 meldet seine Version per `VER:<version>` über UART an den Pico;
  ohne verbundenes Display steht dort `esp32: -`.

### 2.7 Dual-Core-Architektur (RP2350) — Relais schalten IMMER

**Kernanforderung:** Das Schalten der Relais muss *immer* funktionieren, unabhängig
vom LAN-Zustand (kein Kabel, Kabel gezogen/wieder gesteckt, DHCP-Suche, hängende
TCP-Verbindungen). Die blockierenden WIZnet-ioLibrary-Aufrufe (`send`/`recv`/
`sendto`/`disconnect` warten per Busy-Loop auf `Sn_IR_SENDOK`/`Sn_SR`) würden in
einer Single-Loop das Relais-/Display-Handling aushungern. Deshalb ist die Firmware
auf die **zwei Kerne** des RP2350 aufgeteilt:

| | **core0 — Steuerung** | **core1 — Netzwerk** |
|---|---|---|
| Aufgaben | ESP-UART (`esp_link::service`), Relais-GPIO, Rückmeldungen, Impulse, Szenen | W6300, HTTP-Server, DHCP, SSE, **Flash-Schreiben** |
| Blockiert nie? | **ja** (nur GPIO/UART, kein W6300/Flash) | darf blockieren (isoliert von core0) |
| Loop | `net_core_main()` **nicht** — reiner `while`-Loop in `main()` | `net_core_main()` |

- **core0** (`main()` nach `multicore_launch_core1`): `esp_link::service()`,
  `service_relay_feedback()`, `service_impulses()` (beendet abgelaufene, parallel
  laufende Ausgangs-Impulse), Versand des IP-Status. Fasst **niemals** W6300 oder
  Flash an → kann nicht blockieren → Relais reagieren sofort.
- **core1** (`net_core_main()`): `init_network()`, `service_socket()×8`,
  `keepalive_sse()`, `service_network_link()` (LAN-Reconnect) und die Flash-/SSE-
  Aufträge von core0.

**Synchronisation (bewusst minimalistisch, deadlock-frei):**

- Ein einziger `recursive_mutex_t g_state_mtx` schützt den geteilten Steuerzustand
  (Relais-Zustände, Namen/Titel, Szenen, `active_scene`, `esp_fw_version` …). Über
  die RAII-Hülle `struct StateLock`. Nur *ein* Mutex → keine Lock-Reihenfolge →
  Deadlock unmöglich; `recursive_*` erlaubt Wiedereintritt (Helfer rufen Helfer).
- **Regel:** Der Mutex wird **nie** über Netz-I/O oder Flash gehalten. Serializer
  bauen ihren String unter Lock, geben eine Kopie zurück, senden erst danach.
- **Cross-Core-Flags** (`volatile bool`): `g_sse_dirty` (core0→core1: SSE senden),
  `g_persist_dirty` (core0→core1: Flash speichern), `g_ip_status_dirty`
  (core1→core0: IP-/Link-Status ans Display), `g_core1_started`.
- **W6300 ausschließlich auf core1.** Schaltet core0 ein Relais (ESP-Taste), ruft es
  `set_relay()` (unter Lock, GPIO) und setzt `g_sse_dirty`/`g_persist_dirty`; core1
  erledigt Broadcast und Flash. `service_network_link()` fasst die ESP-UART nie an,
  sondern meldet den IP-Status über `g_ip_status_dirty` an core0.
- **Flash-Sicherheit:** `flash_range_erase` macht den XIP-Flash für **beide** Kerne
  unzugänglich. Deshalb schreibt nur core1 und nur über `flash_safe_execute(...)`;
  core0 registriert sich per `flash_safe_execute_core_init()` als Lockout-Victim
  (führt während der Flash-Operation den Multicore-Lockout-Handler aus). `save_config()`
  nutzt vor dem core1-Start (Boot) noch den direkten `save_and_disable_interrupts`-Pfad.
- **Heap:** Beide Kerne nutzen `std::string` → `PICO_USE_MALLOC_MUTEX=1` (in
  `CMakeLists.txt`) macht malloc multicore-sicher.
- CMake: zusätzlich `pico_multicore` und `pico_flash` verlinkt.

### 2.8 Startablauf (`main()`) — wichtig für Boot-Timing

Reihenfolge bewusst so gewählt, damit das Display **unabhängig vom DHCP** früh online geht:

1. `esp_link::init()` **als Erstes** → treibt `GP0` (TX) sofort auf UART-Idle (High),
   damit der ESP beim Kaltstart keine floatende/Break-Leitung sieht.
2. `stdio_init_all()`.
3. Relais- und Config-Init (`init_relays`, `load_config`, `init_users`, `apply_relay`).
   Ab hier stehen Titel/Namen/Zustände fest → Display kann bedient werden.
4. `esp_link::flush_rx()`.
5. `recursive_mutex_init(&g_state_mtx)`, `flash_safe_execute_core_init()` (core0 als
   Lockout-Victim), `g_core1_started = true`, dann `multicore_launch_core1(net_core_main)`.
6. **core0** tritt in den Steuer-Loop ein (`esp_link::service`, IP-Status,
   `service_relay_feedback`, `service_impulses`). **core1** startet parallel `net_core_main()`:
   `init_network()` (DHCP/statisch, darf blockieren), Sockets öffnen, dann
   `service_socket()×8` / `keepalive_sse()` / `service_network_link()` plus Flash-/
   SSE-Aufträge. Der Boot des Displays hängt damit **nicht** mehr am DHCP.

### 2.9 Eingangsmodus: Taster oder Rückmeldung (Compilerschalter)

Die Funktion der Eingänge `FEEDBACK_PINS` (GP10/11/12/13/14/26/27/28) ist ein
**Kompilierzeit**-Schalter — es wird immer nur *ein* Modus in die Firmware gebaut:

- **`INPUT_MODE=rueckm`** — klassische physische Relais-Rückmeldung
  (`service_relay_feedback()`), je Kanal aktivier-/polarisierbar, gemeinsame
  Rückmeldezeit.
- **`INPUT_MODE=taster`** (**Default**) — die Eingänge sind entprellte Taster.
  Aktiviert das Define `INPUT_MODE_BUTTON`.

Umschaltung:

- CMake: `-DINPUT_MODE=taster` bzw. `-DINPUT_MODE=rueckm`
  (`pico/switch_server/CMakeLists.txt`, Default `taster`; ungültiger Wert → Fehler).
- Upload-Skript: `pico/upload.sh -m|--mode taster|rueckm` (Default `taster`),
  auch per Umgebungsvariable `INPUT_MODE`.

Verhalten im Taster-Modus (`INPUT_MODE_BUTTON`):

- Alle Eingänge fix mit **Pull-Up** (`configure_feedback_inputs()`), Taster schaltet
  gegen GND (gedrückt = LOW).
- **Entprellung** über den gemeinsamen Wert `feedback_timeout_ms` (Feld
  „Taster-Entprellzeit", Default `DEFAULT_INPUT_TIME_MS = 25 ms`, Bereich
  `MIN/MAX_INPUT_TIME_MS = 5…2000 ms`).
- `service_buttons()` läuft auf **core0** (parallel zu den Web-/ESP-Buttons). Eine
  steigende Flanke (Druck) **toggelt** das zugehörige Relais (`set_relay()`); im
  Szenen-Modus löst sie stattdessen die Szene `i` aus (`activate_scene()`).
- `state_json()` liefert zusätzlich das Array `buttons` (entprellter Tasterzustand);
  die Konfig-Seite zeigt je Kanal „Taster GPxx" + eine **Pseudo-LED**, die per
  `/state`-Polling (400 ms) den gedrückten Taster anzeigt.
- Die **ESP-Anzeige bleibt unverändert** (kein Taster-spezifisches Protokoll).
- **Persistenz:** gleiche `PersistedConfig`-Struktur wie im Rückmeldemodus; die
  Entprellzeit nutzt das vorhandene `feedback_timeout_ms`-Feld → **keine**
  Versionserhöhung. Die `feedback_*`-Felder sind im Taster-Modus ungenutzt.

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
| `VER:<version>` | ESP32-Firmwareversion (`yy.yyyyy.g<hash>[-dirty][+N]`) melden |
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
# ./upload.sh -m rueckm  # Eingangsmodus Rückmeldung statt Taster (Default: taster)
# ./upload.sh -w         # EINMAL-Werksreset: löscht beim Boot die Persistenz (siehe unten)
# ./upload.sh /pfad/zum/RP2350   # alternatives Ziel-Laufwerk
```

- Pico beim Flashen mit gedrückter **BOOTSEL**-Taste anstecken
  (Ziel-Laufwerk `/media/<user>/RP2350`).
- Eingangsmodus (Taster/Rückmeldung) per `-m|--mode` bzw. `-DINPUT_MODE=` (siehe 2.9).
- Werksreset (Persistenz löschen) per `-w|--wipe-persist` bzw. `-DPERSIST_WIPE=ON` (siehe 6.1).
- Targets: `switch_w6300_relay` (Produktiv), `uart_loopback_test` (Diagnose).
- Serielles Debug/CDC: `/dev/ttyACM0`, 115200.

#### 6.1 Persistenz zurücksetzen / Werksreset (`-w|--wipe-persist`)

Nötig, wenn im Flash ein Konfig-Datensatz mit **inkompatibler `PERSIST_VERSION`**
liegt (z. B. nach einem verworfenen Branch, der das Speicherformat geändert hat).
`load_config()` erkennt dann Magic-ok aber unbekannte Version → Ergebnis
`LayoutChanged` → `main()` setzt `persist_write_locked = true`. Dieser
Downgrade-Schutz **blockiert jedes weitere Speichern** — Einstellungen bleiben
nach Reboot/Reflash nicht erhalten. Ein normales UF2-Flashen hilft nicht, da die
Persistenz-Slots (`0x3F000`, `0x7F000`, `0xFF000`, `PICO_FLASH_SIZE-0x1000`)
**außerhalb** des Programm-Images liegen und dabei unangetastet bleiben.

Der Compilerschalter `PERSIST_WIPE` baut eine Firmware, die beim Boot **einmalig**
`wipe_persist_slots()` ausführt (Erase aller 4 Slots, vor dem core1-Start mit
gesperrten Interrupts). Danach liefert `load_config()` `Empty`, der Write-Lock
entfällt und die aktuelle Version speichert wieder normal.

Ablauf (zwei Flash-Vorgänge, Reihenfolge wichtig):

```bash
cd pico
./upload.sh -w    # 1) Werksreset-Firmware flashen -> löscht beim Boot die Persistenz
                  #    kurz laufen lassen (USB-Log: "PERSIST_WIPE: ... geloescht")
./upload.sh       # 2) normale Firmware flashen -> entfernt den Boot-Wipe
```

- **Schritt 2 ist zwingend:** Solange die `-w`-Firmware läuft, löscht **jeder**
  Boot erneut. Erst die normale Firmware macht die Persistenz wieder dauerhaft.
- `-w` immer zusammen mit dem gewünschten `-m taster|rueckm` angeben (Default `taster`).
- Nach dem Reset gelten die Defaults inkl. Login `admin` / `sw234`.
- `upload.sh` übergibt `-DPERSIST_WIPE=ON|OFF` **immer explizit** an CMake, damit
  ein `ON` nicht im CMake-Cache hängen bleibt und Schritt 2 sicher `OFF` baut.

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
- **WIZnet-Port bleibt unveraendert (eigene Init):** `wizchip_initialize()` im
  WIZnet-Port wartet endlos auf den PHY-Link und wuerde ohne LAN-Kabel den Boot
  (inkl. ESP-Link) blockieren. Die Firmware ruft es daher nicht auf, sondern nutzt
  `wizchip_init_no_phy_wait()` in `relay_server.cpp` (gleicher W6300/QSPI-PIO-Init
  ueber das globale `spi_handle` + `CW_INIT_WIZCHIP`, aber ohne PHY-Warten). Der
  zeitbegrenzte Link-Check erfolgt danach in `wait_for_phy_link()`. Der geklonte
  WIZnet-PICO-C-Code (inkl. `socket.c`) bleibt komplett unveraendert — kein Patch,
  kein Zusatzskript; Blockierungen sind in `relay_server.cpp` per Timeout/`close()`
  umgangen.

