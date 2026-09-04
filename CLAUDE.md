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
    ├── upload.sh                  # Baut + flasht UF2 (Default: USB/picotool ohne BOOTSEL)
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
| `MAX_RELAIS` | 8 | Konfigurierbare Relais (Typ einfach/4-fach) |
| `MAX_BUTTONS` | 8 | Logische Bedien-Buttons (Web + ESP-Display) |
| `MAX_OUTPUTS` | 4 | Ausgänge je Relais (einfach 1, 4-fach 4) |
| `SCENE_COUNT` | 8 | Anzahl konfigurierbarer Szenen |
| `OUTPUT_PINS` | `{2,3,4,5,6,7,8,9}` | Pool wählbarer Ausgangs-GPIOs |
| `INPUT_PINS` | `{10,11,12,13,14,26,27,28}` | Fest gepaarter Eingangs-Pool (Rückmeldung **oder** Taster je Ausgang, siehe 2.9): `OUTPUT_PINS[i]`↔`INPUT_PINS[i]` |
| `DEFAULT_FEEDBACK_TIMEOUT_MS` | 500 | Standardfrist für eine Rückmeldung |
| `DEFAULT_DEBOUNCE_MS` | 25 | Taster-Entprellzeit (Bereich 5–2000) |
| `DEFAULT_IMPULSE_MS` | 300 | Standard-Impulszeit je Relais (Impuls-Modus) |
| `MIN/MAX_IMPULSE_MS` | 100 / 2000 | Grenzen der je Relais einstellbaren Impulszeit |
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

- Struktur `PersistedConfig` (aktuell `PERSIST_VERSION = 9`), passt in einen Flash-Sektor.
- **Keine** Migration von älteren Versionen: bei fremdem Magic/anderer Version liefert
  `load_config()` `LayoutChanged` → Write-Lock → Werksreset per `./upload.sh -w` (siehe 6.1).
- Gespeichert: je Relais (`enabled`, Typ, Name, Ausgangspolarität, Impuls+Impulszeit,
  je Ausgang gewählte Ausgangs-GPIO + Eingangsrolle/-polarität, `active_output`),
  je Button (`enabled`, Name, Relais-Index, Eingang-Index), Titel/Untertitel,
  `public_access`, Benutzer, API-Keys, statische IP/SN/GW, Szenen-Modus + Szenen
  (Name, aktiv-Flag, je **Button** Aktion aus/ein/unverändert), gemeinsame
  Rückmeldezeit und Taster-Entprellzeit. Der Eingang-GPIO wird nicht gespeichert,
  sondern aus der Ausgangs-GPIO abgeleitet (`input_for_output()`).
- CMake-Check `check_persist_overlap.cmake` stellt sicher, dass der Flash-Slot
  nicht mit dem Binär-Image kollidiert.

### 2.4 HTTP-Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/`, `/index.html` | Haupt-UI |
| GET/POST | `/login`, `/logout` | Authentifizierung |
| GET/POST | `/password` | Passwort ändern |
| GET/POST | `/config` | „Ausgänge/Buttons": Titel/Untertitel/`public_access` + je Button (1–8) Aktiv, Name, Zuordnung zu einem Relais-Eingang (Dropdown) |
| GET/POST | `/relais` | „Relais" (Admin): je Relais (1–8) Aktiv, Typ (einfach/2-fach/4-fach), Name, Low aktiv, Impuls+Impulszeit; je Ausgang wählbare Ausgangs-GPIO + Eingangsrolle (keine/Rückmeldung/Taster) + LOW; globale Rückmeldezeit und Taster-Entprellzeit |
| GET/POST | `/network` | Statische IP-Einstellungen |
| GET/POST | `/admin` | Benutzer-/API-Key-Verwaltung |
| GET | `/me` | Aktueller Benutzer |
| GET | `/active_users` | Aktive Sessions/Gäste |
| GET | `/state` | Button-Zustände (JSON: `relays`=Button-EIN, `names`, `feedback_errors`, `scene_mode`, `buttons`=Tasterdruck) |
| GET/POST | `/scenes` | Szenen-Modus + Szenen konfigurieren (Aktion je **Button**, Admin) |
| GET | `/events` | Server-Sent Events (Live-Status) |
| POST | `/relay/<idx>/<on\|off\|toggle>` | Button `idx` (0–7) schalten/umschalten |
| POST | `/scene/<idx>/activate` | Szene `idx` aktivieren |

- **Auth**: Session-Token (Cookie), Rollen, optionaler `public_access`, API-Keys.
- **SSE**: `/events` verteilt Zustandsänderungen live; Keepalive-`ping` alle 1 s.

### 2.5 Szenen-Modus

- Umschaltbar per Checkbox auf `/scenes` (Admin). Persistiert als `scene_mode`.
- Bei aktivem Szenen-Modus steuern die 8 Buttons (Web + ESP-Display) **nicht**
  direkt die Relais, sondern aktivieren je eine Szene.
- Eine **Szene** hat: `enabled`-Flag, Name und je **Button** eine Aktion:
  `0` = aus, `1` = ein, `2` = unverändert. Bei 2-/4-fach-Buttons wirkt nur `1` (wählt
  den Ausgang an); `0` ist bedeutungslos (gegenseitiger Ausschluss) und wird übersprungen.
- Aktivierung ist **momentan** (kein gehaltener Aktiv-Zustand): `activate_scene()`
  wendet die Aktionen an, speichert bei Änderung und meldet den neuen Zustand.
- Direkte Button-Steuerung (`/relay/...`) bleibt parallel nutzbar.

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
  `service_tasters()` (physische Taster), `service_relay_feedback()`,
  `service_impulses()` (beendet abgelaufene, parallel laufende Ausgangs-Impulse),
  Versand des IP-Status. Fasst **niemals** W6300 oder Flash an → kann nicht
  blockieren → Relais reagieren sofort.
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
3. Relais-/Config-Init (`init_relays`, `load_config`, `resolve_gpios`,
   `configure_inputs`, `init_users`, `apply_all_outputs`). Ab hier stehen
   Titel/Namen/Zustände fest → Display kann bedient werden.
4. `esp_link::flush_rx()`.
5. `recursive_mutex_init(&g_state_mtx)`, `flash_safe_execute_core_init()` (core0 als
   Lockout-Victim), `g_core1_started = true`, dann `multicore_launch_core1(net_core_main)`.
6. **core0** tritt in den Steuer-Loop ein (`esp_link::service`, IP-Status,
   `service_tasters`, `service_relay_feedback`, `service_impulses`). **core1** startet parallel `net_core_main()`:
   `init_network()` (DHCP/statisch, darf blockieren), Sockets öffnen, dann
   `service_socket()×8` / `keepalive_sse()` / `service_network_link()` plus Flash-/
   SSE-Aufträge. Der Boot des Displays hängt damit **nicht** mehr am DHCP.

### 2.9 Relais-/Button-Modell und Eingangsrolle (Laufzeit)

Zentrale Abstraktion (kein Compileschalter mehr):

- **Relais** (1–8, Seite `/relais`): Typ `einfach` (1 Eingang → 1 Ausgangs-GPIO),
  `2-fach` (2 Ausgänge) oder `4-fach` (4 Ausgänge). `2-fach`/`4-fach` sind gegenseitig
  ausschließend — „immer genau ein Ausgang aktiv". Je Relais optional **Impuls** (nur der gewählte Ausgang wird
  gepulst/gesetzt, die Anzeige `active_output` bleibt gelatcht) und Ausgangspolarität.
- **Ausgangs-GPIO** pro Ausgang frei aus `OUTPUT_PINS` wählbar; der **Eingang-GPIO**
  ergibt sich fest daraus (`input_for_output()`: `OUTPUT_PINS[i]`↔`INPUT_PINS[i]`).
  `resolve_gpios()` validiert (kein Ausgangs-Pin doppelt) und leitet `in_gpio` ab.
- **Eingangsrolle je Ausgang** (Laufzeit, Dropdown auf `/relais`):
  - `Rückmeldung` — `service_relay_feedback()` prüft `in_gpio` gegen den erwarteten
    Zustand, Polarität je Ausgang; gemeinsame `feedback_timeout_ms`.
  - `Taster` — `service_tasters()` (core0) entprellt (`taster_debounce_ms`, Pull-Up,
    gedrückt = LOW) und löst bei steigender Flanke denselben Relais-Eingang aus wie
    der zugehörige logische Button.
  - `keine` — Eingang ungenutzt.
- **Buttons** (1–8, Seite `/config`): logische Bedienelemente (Web + ESP-Display),
  jeder verweist explizit auf einen Relais-Eingang. Ein `einfach`-Relais belegt 1
  Button-Ziel, ein `4-fach`-Relais 4 (gegenseitig ausschließend → auf dem Display
  ist stets nur einer der 4 „ON").
- Kern-Naht: `activate_relais_input()`/`button_command()` (logische Buttons + ESP
  `SWn` + `/relay/<idx>`) und `service_tasters()` (physische Taster) laufen über
  dieselbe Aktion; die **ESP-Anzeige/Protokoll bleiben button-indiziert unverändert**.
- `state_json()` liefert `relays` (Button-EIN), `feedback_errors` (Fehler des
  referenzierten Ausgangs) und `buttons` (Tasterdruck des referenzierten Ausgangs).

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
./upload.sh              # baut UF2 und flasht per picotool OHNE BOOTSEL-Taste (Default)
# ./upload.sh -c         # Clean-Build erzwingen
# ./upload.sh -w         # EINMAL-Werksreset: löscht beim Boot die Persistenz (siehe unten)
# ./upload.sh -d         # klassisch: UF2 auf BOOTSEL-Laufwerk kopieren
# ./upload.sh /pfad/zum/RP2350   # UF2 auf bestimmtes BOOTSEL-Laufwerk kopieren
```

- **Default (ohne BOOTSEL):** Der laufende Pico wird per `picotool` über USB in
  BOOTSEL versetzt, geflasht und neu gestartet. Voraussetzung: laufende Firmware
  per USB verbunden + `picotool` (mit USB-Support) installiert.
- **BOOTSEL-Laufwerk (`-d`/`--drive` bzw. UPLOAD_DIR-Argument):** Pico mit
  gedrückter **BOOTSEL**-Taste anstecken (Ziel-Laufwerk `/media/<user>/RP2350`).
- Eingangsrolle (Rückmeldung/Taster) ist eine **Laufzeit**-Option je Relais-Ausgang
  (Seite `/relais`, siehe 2.9) — kein Build-Schalter mehr.
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
- Reihenfolge beachten: erst `-w` flashen (löscht die Persistenz), dann ohne `-w` neu flashen.
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

