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

Zentrale Backend-Firmware. Enthält die Szene→Schalter→GPIO-Steuerung, HTTP-Server,
Persistenz, Authentifizierung, SSE-Live-Updates und den ESP-Link.

### 2.1 Konfiguration (`namespace cfg`)

| Konstante | Wert | Bedeutung |
|---|---|---|
| `RELAY_COUNT` / `CHANNEL_COUNT` | 8 | Anzahl physischer Kanäle (Ausgang + Eingang) |
| `SWITCH_COUNT` | 8 | Anzahl logischer Schalter (GPIO-Gruppen) |
| `SCENE_COUNT` | 8 | Anzahl Szenen (= Display-Buttons) |
| `MAX_BINDINGS` | 8 | max. GPIO-Bindungen je Schalter |
| `RELAY_PINS` | `{2,3,4,5,6,7,8,9}` | GPIO-Ausgänge der Kanäle |
| `FEEDBACK_PINS` | `{10,11,12,13,14,26,27,28}` | GPIO-Eingänge je Kanal: Rückmeldung **oder** lokaler Taster (Laufzeit je Kanal, siehe 2.9) |
| `RELAY_ACTIVE_LOW` | `false` | Alt-Default (Polarität ist jetzt je GPIO-Bindung) |
| `DEFAULT_FEEDBACK_TIMEOUT_MS` | 500 | globale Rückmeldezeit statischer Ausgänge (10–10000) |
| `MIN/MAX/DEFAULT_IMPULSE_MS` | 100 / 2000 / 500 | Impulszeit je Impuls-GPIO |
| `BUTTON_DEBOUNCE_MS` | 25 | Entprellzeit lokaler Taster |
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

- Struktur `PersistedConfig` (aktuell `PERSIST_VERSION = 8`), passt in einen Flash-Sektor
  (~2,6 KB von 4096). `check_persist_overlap.cmake` prüft die Kollision mit dem Binär-Image.
- Gespeichert: **Schalter** (Name, Ist-Zustand, je Bindung Kanal + Flags
  `active_low/impulse/impulse_trig/feedback_en/feedback_low` + Impulszeit),
  **Szenen** (aktiv-Flag, Name, je Schalter Aktion aus/ein/unverändert),
  **Kanal-Eingänge** (Rolle Rückmeldung/Taster + Taster-Szene), globale Rückmeldezeit,
  `active_scene`, Titel/Untertitel, `public_access`, Benutzer, API-Keys, statische IP/SN/GW.
- **Migration**: `V7→V8` bildet die 8 alten Relais 1:1 auf 8 Schalter mit je einer Bindung ab
  (`build_switches_1to1`), Szenen-Aktionen und Namen bleiben erhalten. Ältere Layouts
  (`V1..V6`, Vorserie) werden **nicht** migriert (`LayoutChanged`, Flash bleibt unangetastet).

### 2.4 HTTP-Endpunkte

| Methode | Pfad | Zweck |
|---|---|---|
| GET | `/`, `/index.html` | Haupt-UI |
| GET/POST | `/login`, `/logout` | Authentifizierung |
| GET/POST | `/password` | Passwort ändern |
| GET/POST | `/config` | Titel/Untertitel/`public_access`, globale Rückmeldezeit (statisch); je Kanal Eingangsrolle **Rückmeldung/Taster** + (bei Taster) zugeordnete Szene |
| GET/POST | `/switches` | **Schalter-Editor** (Admin): je Schalter Name + 1–8 GPIO-Bindungen (Kanal, *Low aktiv*, *Impuls* + Trigger *Ein+Aus/nur Ein/nur Aus* + Impulszeit, *Rückmeldung*, *Rückm. LOW*) |
| GET/POST | `/scenes` | **Szenen-Editor** (Admin): je Szene aktiv-Flag, Name, je Schalter Aktion aus/ein/unverändert |
| GET/POST | `/network` | Statische IP-Einstellungen |
| GET/POST | `/admin` | Benutzer-/API-Key-Verwaltung |
| GET | `/me` | Aktueller Benutzer |
| GET | `/active_users` | Aktive Sessions/Gäste |
| GET | `/state` | Zustand (JSON): `scenes[]`, `active_scene`, `scene_pending[]` (gelb), `scene_errors[]`, `switch_states[]` |
| GET | `/events` | Server-Sent Events (Live-Status) |
| POST | `/scene/<idx>/activate` | Szene `idx` aktivieren (= Display-Button) |

- **Auth**: Session-Token (Cookie), Rollen, optionaler `public_access`, API-Keys.
- **SSE**: `/events` verteilt Zustandsänderungen live; Keepalive-`ping` alle 1 s.
- Der frühere Direkt-Toggle `POST /relay/...` **entfällt** (alles läuft über Szenen).

### 2.5 Funktionsmodell: Szene → Schalter → GPIO

Die 8 Display-Buttons **sind** die Szenen. Es gibt keinen separaten Direktbetrieb mehr.

- **Szene** (`activate_scene`): momentane Aktivierung wie bisher. `enabled`-Flag, Name und
  je **Schalter** eine Aktion `0`=aus / `1`=ein / `2`=unverändert. Zeigt „zuletzt aktiviert".
- **Schalter** (`struct Switch`, `set_switch`): logische Funktion mit Ist-Zustand und 1–8
  **GPIO-Bindungen**. `set_switch(w,on,scene)` treibt alle Bindungen und startet je Bindung
  das Rückmeldefenster; Rückgabe = Zustand geändert (Persistenz nur bei Änderung).
- **GPIO-Bindung** (`struct GpioBinding`): Kanal 0–7 + Verhalten `active_low`, `impulse`
  (+ `impulse_trig` Ein+Aus/nur Ein/nur Aus, + `impulse_ms`), `feedback_en`, `feedback_low`.
- **Impulse** laufen **parallel** über `channel_impulse_deadline[]`; `service_impulses()`
  (core0) beendet jeden Kanal unabhängig, ohne zu blockieren.
- **Rückmeldung** wird UND-verknüpft aggregiert (Bindung → Schalter → Szene, nach
  auslösender Szene): `service_switch_feedback()` prüft statische Bindungen laufend
  (Treffer löscht, Timeout → Fehler) und Impuls-Bindungen einmalig **nach** Ablauf der
  Impulszeit gegen den Schalter-Sollzustand. Während des Wartens ist `scene_pending`
  gesetzt → Button **gelb**; ein Fehler → `scene_error` → **rot**.
- **Sharing**: ein GPIO darf in mehreren Schaltern, ein Schalter in mehreren Szenen liegen;
  bei Widerspruch am Ausgang gewinnt der zuletzt ausgeführte Befehl.
- Der Spezialfall *Szene → 1 Schalter → 1 statischer GPIO* reproduziert den früheren
  Direkt-Button.

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
| Aufgaben | ESP-UART (`esp_link::service`), GPIO, Impulse, Rückmeldungen, Taster, Szenen | W6300, HTTP-Server, DHCP, SSE, **Flash-Schreiben** |
| Blockiert nie? | **ja** (nur GPIO/UART, kein W6300/Flash) | darf blockieren (isoliert von core0) |
| Loop | `net_core_main()` **nicht** — reiner `while`-Loop in `main()` | `net_core_main()` |

- **core0** (`main()` nach `multicore_launch_core1`): `esp_link::service()`,
  `service_impulses()` (Impulse beenden), `service_buttons()` (lokale Taster →
  Szene), `service_switch_feedback()` (Rückmeldung → gelb/rot), Versand des
  IP-Status. Fasst **niemals** W6300 oder Flash an → kann nicht blockieren → Ausgänge
  reagieren sofort.
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
- **W6300 ausschließlich auf core1.** Löst core0 eine Szene aus (ESP-Taste/lokaler
  Taster), ruft es `activate_scene()` → `set_switch()` (unter Lock, GPIO/Impuls-Timer)
  und setzt `g_sse_dirty`/`g_persist_dirty`; core1 erledigt Broadcast und Flash.
  `service_network_link()` fasst die ESP-UART nie an, sondern meldet den IP-Status
  über `g_ip_status_dirty` an core0.
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
3. GPIO-/Config-Init (`init_relays` = Kanäle als Ausgang, `load_config`,
   `configure_inputs`, `init_users`, `apply_all_switches` = statische Ausgänge auf
   geladene Schalter-Zustände). Ab hier stehen Titel/Namen/Zustände fest → Display bedienbar.
4. `esp_link::flush_rx()`.
5. `recursive_mutex_init(&g_state_mtx)`, `flash_safe_execute_core_init()` (core0 als
   Lockout-Victim), `g_core1_started = true`, dann `multicore_launch_core1(net_core_main)`.
6. **core0** tritt in den Steuer-Loop ein (`esp_link::service`, IP-Status,
   `service_impulses`/`service_buttons`/`service_switch_feedback`). **core1** startet parallel `net_core_main()`:
   `init_network()` (DHCP/statisch, darf blockieren), Sockets öffnen, dann
   `service_socket()×8` / `keepalive_sse()` / `service_network_link()` plus Flash-/
   SSE-Aufträge. Der Boot des Displays hängt damit **nicht** mehr am DHCP.

### 2.9 Kanal-Eingänge: Rückmeldung oder lokaler Taster (Laufzeit, je Kanal)

Die Rolle jedes Eingangs `FEEDBACK_PINS[i]` (GP10/11/12/13/14/26/27/28) ist eine
**Laufzeit**-Option je Kanal auf `/config` — **kein** Compilerschalter mehr
(`INPUT_MODE` ist entfernt). `configure_inputs()` setzt alle Eingänge fix auf **Pull-Up**.

- **Rolle „Rückmeldung"** (`IN_FEEDBACK`, Default): der Pin wird von den GPIO-Bindungen
  mit `feedback_en` ausgewertet (Polarität je Bindung über `feedback_low`). Ist ein
  Kanal als Taster konfiguriert, wird `feedback_en` auf diesem Kanal ignoriert.
- **Rolle „Taster"** (`IN_BUTTON`): entprellter lokaler Taster gegen GND (gedrückt = LOW).
  `service_buttons()` (core0) löst bei steigender Flanke die dem Kanal **zugeordnete
  Szene** aus (`channel_input[i].scene`). Entprellzeit fix `BUTTON_DEBOUNCE_MS = 25 ms`.

Hinweis: Da der Pull-Up fix ist, eignen sich Rückmeldungen am besten als
Öffner/Schließer gegen GND mit `feedback_low` (aktiv = LOW). Persistiert wird die Rolle
je Kanal in `PersistedChannelInput` (siehe 2.3).

---

## 3. ESP32-CYD-Display (`esp32/src/main.cpp`)

Lokales Touch-Terminal (Board ESP32-2432S028, „CYD"), LVGL 8 + TFT_eSPI + XPT2046-Touch.

- **UI**: 8 Buttons = **Szenen** (der Pico sendet immer `MODE:SCENE`). Die Buttons zeigen
  die Szenennamen; nur aktivierte Szenen sind sichtbar, ein Tastendruck löst die Szene
  aus (momentan, `SCENEn:GO`). Startanzeige „wait for init", bis der Pico antwortet.
- **Button-Farben** (`update_switch_visual`, Priorität): **gelb** = wartet auf
  Rückmeldung (`WAITn:ON`), **rot** = Rückmeldefehler (`SERRORn:ON`), **grün** = aktive
  Szene (`ASCENE`), **cyan** = inaktiv.
- **Pico-Link**: `PICO_UART = Serial` (UART0, `GPIO1` = TX, `GPIO3` = RX), 115200 Baud.
  Diese Schnittstelle wird **ausschließlich** für die Pico-Kommunikation genutzt —
  keine Debug-Ausgaben darüber.
- **Heartbeat**: sendet `PING` alle 1000 ms; `pico_online` wird bei `PONG` gesetzt,
  Timeout nach 3000 ms → zurück auf „wait for init".
- Nach `pico_online` wird `GET DISPLAY` abgefragt und die UI aktualisiert.
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
| `SCENEn:GO` | Szene `n` (1–8) auslösen (= Button-Druck) |

### Pico → ESP32

| Antwort | Bedeutung |
|---|---|
| `PONG` | Antwort auf `PING` |
| `TITLE:<text>` | Seitentitel |
| `SUBTITLE:<text>` | Untertitel |
| `NAMEn:<text>` | Name von Schalter `n` (Fallback „Schalter n"; nur für Gate/Alt-Modus) |
| `MODE:SCENE` | Betriebsart (immer szenenbasiert) |
| `SCENEn:<text>` | Name von Szene `n` (leer = Szene inaktiv) |
| `STATEn:ON` / `STATEn:OFF` | Szene `n` ist die aktive Szene? |
| `WAITn:ON` / `WAITn:OFF` | Szene `n` wartet auf Rückmeldung → **gelb** |
| `SERRORn:ON` / `SERRORn:OFF` | Rückmeldefehler der Szene `n` → **rot** |
| `ASCENE:n` | aktive Szene (0 = keine) |
| `SDIRTY:ON/OFF` | aktive Szene überschrieben (derzeit stets OFF) |
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
- Eingangsrolle (Rückmeldung/Taster) ist Laufzeit je Kanal auf `/config` (siehe 2.9);
  kein Compilerschalter mehr.
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
- **Modell Szene→Schalter→GPIO** (funktionszentriert): 8 Display-Buttons = Szenen →
  1–8 Schalter → 1–8 GPIO-Bindungen. Direkter Relais-Toggle (`/relay/…`, `SWn:…`) ist
  entfernt (siehe 2.4/2.5). Config-Reihenfolge: erst `/switches`, dann `/scenes`.
- Rückmeldung: alle definierten Bindungen eines Schalters müssen stimmen (UND);
  Impuls-Bindungen werden erst nach Ablauf der Impulszeit bewertet; warten = gelb.
- Neuer Kanal/Schalter braucht erst Bindungen auf `/switches`, sonst schaltet die Szene
  nichts. Eingänge sind fix Pull-Up (Rückmeldung idealerweise `feedback_low`, aktiv=LOW).
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

