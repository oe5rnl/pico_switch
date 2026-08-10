# 8 Kanal Relais-Webserver (C++ / RP2350 + W6300)

Native C++17-Firmware fuer Raspberry Pi Pico 2 (RP2350) mit WIZnet W6300 Ethernet-Modul (QSPI Quad). Steuert acht Relais ueber Webinterface und HTTP-API, mit Benutzer- und API-Key-Verwaltung, Server-Sent Events fuer Live-Updates, DHCP/static-Netzwerkmodus und persistenter Konfiguration im Flash.

Der gesamte Firmware-Code liegt in einer einzelnen Quelldatei: [switch_server/src/relay_server.cpp](switch_server/src/relay_server.cpp).

> Diese Datei beschreibt die Pico-Firmware im Detail. Projektueberblick, Verkabelung
> Display <-> Pico, Schnellstart und die Installation aller benoetigten Tools stehen in
> der [Root-README](../../README.md).

## Verzeichnisstruktur

```
pico/
├── upload.sh                       Build + lokale UF2-Kopie + Upload zum Pico
├── README.md                       diese Datei
├── switch_w6300_relay_native.uf2   letzte erfolgreiche Build-Kopie
├── w6300_official_test/            Referenz-Beispielprojekt von WIZnet
└── switch_server/
    ├── CMakeLists.txt
    ├── cmake/
    │   └── check_persist_overlap.cmake   Build-Time-Schutz Firmware vs. Flash-Slots
    ├── src/
    │   ├── relay_server.cpp              Komplette Firmware
    │   └── uart_loopback_test.cpp        UART0-Loopback-Diagnose
    └── build/                            CMake-Build-Output
```

## Hardware

| Komponente            | Konfiguration                                    |
| --------------------- | ------------------------------------------------ |
| MCU                   | Raspberry Pi Pico 2 (RP2350)                     |
| Ethernet-Chip         | WIZnet W6300 ueber QSPI (Quad-Mode, PIO)         |
| Relais-Anzahl         | 8                                                |
| Relais-GPIOs          | GP2 ... GP9                                      |
| Rueckmelde-GPIOs      | GP10 ... GP14, GP26 ... GP28                    |
| Relais-Logik          | active-high (`RELAY_ACTIVE_LOW = false`)         |
| MAC-Adresse           | `DE:AD:BE:EF:63:02`                              |
| IPv4                  | `192.168.88.188 / 255.255.255.0`                 |
| Gateway / DNS         | `192.168.88.254` / `1.1.1.1`                     |
| DHCP-Bootstrap        | GP15 / Pin 20 mit Pull-up: HIGH -> DHCP, LOW -> static |
| HTTP-Port             | 80                                               |
| Gleichzeitige Sockets | 8 (W6300), davon max. 6 fuer SSE                 |

Die SPI/QSPI-Pinbelegung wird vom WIZnet-PICO-C-Port gesetzt und nicht in `relay_server.cpp` definiert.

### IP-Modus beim Start

Beim Start liest die Firmware GP15 (physischer Pin 20) mit internem Pull-up:

- GP15 offen oder HIGH: DHCP wird gestartet und die IPv4-Adresse vom DHCP-Server uebernommen.
- GP15 auf GND: statische Adresse wird verwendet.

Die statische Adresse ist im Auslieferungszustand `192.168.88.188 / 255.255.255.0` mit Gateway `192.168.88.254` und kann auf der Seite `/network` geaendert und persistent gespeichert werden. Wenn DHCP nach mehreren Versuchen keine Adresse liefert oder ein Adresskonflikt gemeldet wird, faellt die Firmware auf diese gespeicherten statischen Werte zurueck. Die tatsaechlich verwendete Adresse wird ueber USB-Serial ausgegeben, z.B. `HTTP-Server: http://192.168.88.188/`.

## Build

### Voraussetzungen

- CMake >= 3.13
- ARM-Toolchain `arm-none-eabi-gcc` / `g++` (C11, C++17)
- `build-essential` (make, host-gcc) und `git`
- Eine ausgecheckte Kopie von [WIZnet-PICO-C](https://github.com/WIZnet-ioNIC/WIZnet-PICO-C) inkl. Submodulen (Pico SDK, ioLibrary_Driver). Der Standard-Checkout liegt unter `pico/WIZnet-PICO-C` und ist per `.gitignore` von der Versionierung ausgenommen. Der Pfad ist ueber `WIZNET_PICO_C_PATH` in `switch_server/CMakeLists.txt`, via `-D` oder als Umgebung fuer `upload.sh` ueberschreibbar.

Die Installation der Toolchain (CMake, `arm-none-eabi-gcc`, `build-essential`, `git`)
ist in der [Root-README](../../README.md#installation-der-benoetigten-tools) beschrieben.

### WIZnet-PICO-C laden oder aktualisieren

`upload.sh` laedt den benoetigten Code automatisch nach `pico/WIZnet-PICO-C`, falls der Ordner noch nicht existiert. Manuell (im `pico/`-Verzeichnis ausfuehren):

```bash
git clone --recursive https://github.com/WIZnet-ioNIC/WIZnet-PICO-C.git WIZnet-PICO-C
cd WIZnet-PICO-C
git submodule update --init --recursive
```

Falls der Ordner schon existiert, aber Submodule fehlen oder unvollstaendig sind:

```bash
cd WIZnet-PICO-C
git pull --ff-only
git submodule update --init --recursive
```

Wer den Checkout anders ablegen moechte, kann einen anderen Pfad verwenden und ihn beim Konfigurieren an CMake uebergeben:

```bash
git clone --recursive https://github.com/WIZnet-ioNIC/WIZnet-PICO-C.git ~/src/WIZnet-PICO-C
cd switch_server
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWIZNET_PICO_C_PATH=$HOME/src/WIZnet-PICO-C
cmake --build build -j$(nproc)
```

### Manuell

```bash
cd switch_server
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Ergebnis: `switch_server/build/switch_w6300_relay.uf2`.

### Mit `upload.sh`

Das Skript liegt eine Ebene hoeher in `pico/`:

```bash
cd pico
./upload.sh                  # Default-Ziel: /media/$USER/RP2350
./upload.sh -c              # Clean-Build (Build-Verzeichnis vorher loeschen)
./upload.sh /pfad/zum/RP2350 # alternatives Mountpoint
```

Das Skript

1. konfiguriert und baut Release in `switch_server/build/`,
2. kopiert die UF2-Datei nach `switch_w6300_relay_native.uf2`,
3. kopiert sie zusaetzlich auf das angegebene Pico-Laufwerk (BOOTSEL-Modus) und ruft `sync` auf.

### Build-Time-Schutz `check_persist_overlap.cmake`

Nach jedem Link wird `__flash_binary_end` mit der Adresse des ersten Persistenz-Slots (`0x1003F000`) verglichen. Liegt das Firmware-Ende dahinter, bricht der Build mit `FATAL_ERROR` ab; sonst erscheint eine Statuszeile mit dem freien Platz.

## Flashen

1. `BOOTSEL`-Taster gedrueckt halten und USB anstecken.
2. Pico erscheint als USB-Massenspeicher `RP2350`.
3. `switch_w6300_relay_native.uf2` darauf kopieren (
  oder `upload.sh` benutzen).
4. Pico startet automatisch neu.

Der RP2350-Bootloader loescht beim UF2-Drop nur Sektoren, die im UF2 enthalten sind. Da alle Persistenz-Slots ausserhalb der Firmware-Region liegen, bleiben Konfiguration, Benutzer und API-Keys erhalten.

Funktionspruefung:

```bash
ping 192.168.88.188
curl http://192.168.88.188/state
```

## Default-Login

| Feld     | Wert                                                                |
| -------- | ------------------------------------------------------------------- |
| Benutzer | `admin`                                                             |
| Passwort | `sw234`                                                             |
| Hash     | `01d1b800ad11a2bc64e75374283bb735a2e9fe0df3f142c73b740962adc07683`  |

Passwoerter werden als SHA-256-Hex gespeichert. Beim ersten Start ohne gespeicherte Konfiguration wird der Default-Admin angelegt. Ein in der Persistenz vorhandener Legacy-Admin-Hash (`8c6976e5...`) wird automatisch auf den neuen Default-Hash migriert.

## Web-Oberflaeche

Alle Seiten werden direkt aus `relay_server.cpp` ausgeliefert; es gibt keine externen Web-Assets.

| Pfad        | Zweck                                                                  | Zugriff             |
| ----------- | ---------------------------------------------------------------------- | ------------------- |
| `/`         | Hauptseite mit Relais-Kacheln (bzw. Szenen im Szenen-Modus) und Live-Status | oeffentlich (Public-Flag) bzw. angemeldet |
| `/login`    | Anmeldeformular mit Abbruch-Button                                     | oeffentlich         |
| `/logout`   | Beendet die Session und loescht den Admin-Persistent-Token             | jede Session        |
| `/password` | Passwortaenderung mit Abbruch-Button                                   | jede Session        |
| `/config`   | Seitentitel, Untertitel, Relais-Namen (mit Ausgangs-GPIO), Public-Flag, Ausgangspolaritaet (Low aktiv), Rueckmeldung + Rueckmeldezeit | nur Admin           |
| `/scenes`   | Szenen-Modus umschalten und die 8 Szenen (Name, aktiv, Aktion je Kanal) konfigurieren | nur Admin           |
| `/network`  | Aktuelle Netzwerkwerte plus persistente Static-IP/Subnet/Gateway       | nur Admin           |
| `/admin`    | Benutzer- und API-Key-Verwaltung                                       | nur Admin           |

Eigenschaften der Hauptseite:

- Der initiale Relaiszustand (Namen, States, aktive Benutzer) wird per `state_json()` direkt ins HTML eingebettet. Bei Refresh oder Klick auf den Seitentitel werden keine Default-Werte mehr kurz angezeigt.
- Die Relais-Kacheln sind selbst die Schaltflaechen; `ON`/`OFF` wird nur als Status-Text angezeigt.
- Die Relais-Kacheln sind nur sichtbar, wenn `can_control` gilt (Public-Mode an, oder eingeloggt, oder gueltiger Admin-Persistent-Token).

Eigenschaften der Admin-Seiten:

- `/config`, `/network` und `/admin` haben die Navigations- bzw. Speichern-Buttons direkt unter der Seitenueberschrift.
- `/network` zeigt im oberen Bereich die aktuell laufenden Werte aus dem WIZnet-Chip: Mode (`DHCP` oder `Static`), MAC, IP, Subnet Mask, Gateway und DNS.
- Im Bereich `Static` auf `/network` koennen IP, Subnet Mask und Gateway geaendert werden. Diese Werte werden im Flash gespeichert und beim naechsten Static-Start bzw. als DHCP-Fallback verwendet. DHCP-Laufzeitwerte ueberschreiben diese Static-Konfiguration nicht.

### Verbindungsstatus

Alle relevanten Seiten enthalten eine Statusanzeige `Verbunden` / `Getrennt`, gespeist aus einer `EventSource('/events')`-Verbindung:

- Server sendet jede Sekunde ein `event: ping`.
- Bei jeder Relais- oder Konfigaenderung sendet er zusaetzlich ein `event: state` mit dem aktuellen JSON-Zustand.
- Der Client laeuft im 1-s-Takt; bleibt SSE-Aktivitaet ueber 2,5 s aus, wird `Getrennt` angezeigt und die EventSource aktiv neu aufgebaut.
- Beim Verlassen der Seite (`pagehide`) wird die EventSource geschlossen, damit keine Sockets im W6300 haengen bleiben.
- Der Server begrenzt parallele SSE-Verbindungen auf `MAX_SSE_SOCKETS` (= `HTTP_SOCKET_COUNT - 2`, also 6). Sind mehr offen, antwortet `/events` mit `503 Service Unavailable` und Header `Retry-After: 1`. Der Client respektiert das und versucht es erneut.

## HTTP-API

Authentifizierungswege fuer geschuetzte Endpunkte:

- Cookie `sid` aus einer Session (per `/login` gesetzt)
- HTTP-Header `X-API-Key: <key>` oder Query-Parameter `?api_key=<key>`
- Public-Flag (nur fuer `/state` und `/relay/<n>/...`)

**Sind keine API-Keys konfiguriert, werden auch nicht API-Aufrufe nicht durch Keys eingeschraenkt.**

### Allgemein

| Methode | Pfad             | Auth                          | Antwort                                      |
| ------- | ---------------- | ----------------------------- | -------------------------------------------- |
| GET     | `/`              | siehe oben                    | Hauptseite (HTML)                            |
| GET     | `/state`         | Session / API-Key / Public    | JSON: `relays`, `names`, `title`, `subtitle`, `public`, `active_users` |
| GET     | `/me`            | anonym                        | `{"username": ..., "role": ...}` oder `null` |
| GET     | `/active_users`  | anonym                        | `{"active_users":[{"username","role","remaining"},...]}` |
| GET     | `/events`        | anonym                        | `text/event-stream` oder `503 Retry-After: 1` |

### Relais

| Methode | Pfad                        | Auth                       |
| ------- | --------------------------- | -------------------------- |
| POST    | `/relay/<0..7>/toggle`      | Session / API-Key / Public |
| POST    | `/relay/<0..7>/on`          | Session / API-Key / Public |
| POST    | `/relay/<0..7>/off`         | Session / API-Key / Public |
| POST    | `/scene/<0..7>/activate`    | Session / API-Key / Public |

`/scene/<n>/activate` wendet die Aktionen der (aktivierten) Szene `n` momentan an. Die direkte Relais-Steuerung bleibt parallel nutzbar.

Beispiele:

```bash
curl http://192.168.88.188/state
curl -X POST http://192.168.88.188/relay/0/toggle
curl -X POST -H 'X-API-Key: abcdef0123456789' http://192.168.88.188/relay/3/on
```

### Anmeldung und Passwort

| Methode | Pfad        | Body / Query                              | Wirkung                                  |
| ------- | ----------- | ----------------------------------------- | ---------------------------------------- |
| GET     | `/login`    | `?next=/path`                             | HTML-Formular                            |
| POST    | `/login`    | Form: `username`, `password`, `next`      | 302 nach `next`, setzt Cookie `sid`      |
| GET     | `/logout`   | -                                         | 302 nach `/`, loescht `sid`              |
| GET     | `/password` | -                                         | HTML-Formular                            |
| POST    | `/password` | Form: `old`, `new1`, `new2`               | Setzt neues Passwort der eigenen Session |

### `/config` (Admin)

`POST` mit JSON, alle Felder optional bzw. wie unten gezeigt:

```json
{
  "names":            ["Relais 1", "Relais 2", "...", "Relais 8"],
  "title":            "Hauptseiten-Titel",
  "subtitle":         "Untertitel / Header",
  "public":           false,
  "act_low":          [false, false, false, false, false, false, false, false],
  "feedback_enabled": [false, false, false, false, false, false, false, false],
  "feedback_low":     [false, false, false, false, false, false, false, false],
  "feedback_timeout": 500
}
```

- `act_low` je Kanal: `true` = Ausgang LOW-aktiv, `false` = HIGH-aktiv.
- `feedback_enabled` je Kanal aktiviert die Ueberwachung des Rueckmeldeeingangs.
- `feedback_low` je Kanal: aktiver Rueckmeldepegel ist LOW.
- `feedback_timeout` ist die gemeinsame Frist in ms (`10` bis `10000`, Standard `500`).
- Auf der HTML-Seite zeigen die Namensfelder zusaetzlich die zugehoerige Ausgangs-GPIO an (z.B. `Relais 1 (GP2)`).

### `/scenes` (Admin)

`GET /scenes` liefert die Szenen-Konfigurationsseite. `POST /scenes` speichert Szenen-Modus und die 8 Szenen als JSON mit flachen Schluesseln je Szene `s0_`..`s7_`:

```json
{
  "mode":    false,
  "s0_en":   true,
  "s0_name": "Alles aus",
  "s0_act":  "00000000"
}
```

- `mode`: Szenen-Modus aktiv (Tasten aktivieren Szenen statt Relais).
- `sN_en`: Szene `N` aktiviert/sichtbar.
- `sN_name`: Szenenname (max. 32 Zeichen).
- `sN_act`: 8-Zeichen-String, je Kanal `0` = aus, `1` = ein, `2` = unveraendert.

### `/network` (Admin)

`GET /network` zeigt aktuelle Netzwerkwerte und die gespeicherte Static-Konfiguration.

`POST /network` speichert neue statische IPv4-Werte als JSON:

```json
{
  "ip":      "192.168.88.188",
  "subnet":  "255.255.255.0",
  "gateway": "192.168.88.254"
}
```

Antworten sind `{"ok":true}` oder `400` mit `{"ok":false,"error":"ungueltige IPv4-Adresse"}`. Die neuen Werte werden nach dem Speichern persistent abgelegt und beim naechsten Static-Start aktiv.

### `/admin` (Admin)

`POST` mit JSON, das Feld `action` waehlt die Aktion:

| `action`           | Felder                                       | Wirkung                                                              |
| ------------------ | -------------------------------------------- | -------------------------------------------------------------------- |
| `add`              | `username`, `password`, `role` (`admin`/`user`) | Benutzer anlegen, Passwort min. 4 Zeichen                          |
| `delete`           | `username`                                   | Benutzer loeschen (eigener Account ist gesperrt)                     |
| `set_password`     | `username`, `password`                       | Passwort eines Benutzers neu setzen                                  |
| `set_role`         | `username`, `role`                           | Rolle aendern; letzter Admin kann nicht herabgestuft werden; bei Degradierung werden Sessions des Users verworfen |
| `gen_key`          | `comment` (optional)                         | Erzeugt 16-Hex-Zeichen-Key, max. 8 Keys insgesamt                    |
| `set_key_comment`  | `key`, `comment`                             | Aendert Kommentar (max. 64 Zeichen) eines Keys                       |
| `delete_key`       | `key`                                        | Key loeschen                                                         |

Antworten sind `{"ok":true,...}` oder `4xx`-JSON mit Fehlertext.

## Display-Anbindung (ESP32 / UART)

Ein ESP32-CYD-Touchdisplay ist optional ueber UART0 angebunden (`GP0` = TX, `GP1` = RX, 115200 Baud, 8N1). Der zustaendige Code liegt im `namespace esp_link` und laeuft unabhaengig vom Netzwerk, damit das Display auch ohne DHCP frueh bedienbar ist.

- `stdio` laeuft nur ueber USB (`pico_enable_stdio_uart = 0`), damit `printf`-Debug die UART-Leitung nicht stoert.
- `esp_link::init()` wird als Erstes in `main()` aufgerufen und treibt `GP0` sofort auf Idle-High.
- Bei jeder Zustands-, Namens-, Titel- oder Modusaenderung werden Updates ans Display gesendet.

Zeilenbasiertes Textprotokoll (Auszug):

| Richtung        | Nachricht                | Bedeutung                                   |
| --------------- | ------------------------ | ------------------------------------------- |
| ESP32 -> Pico   | `PING`                   | Heartbeat                                   |
| ESP32 -> Pico   | `GET DISPLAY`            | Vollstaendige Display-Konfiguration anfordern |
| ESP32 -> Pico   | `SWn:ON` / `SWn:OFF`     | Relais `n` (1..8) schalten                  |
| ESP32 -> Pico   | `SCENEn:GO`              | Szene `n` (1..8) ausloesen                  |
| Pico -> ESP32   | `PONG`                   | Antwort auf `PING`                          |
| Pico -> ESP32   | `TITLE:` / `SUBTITLE:` / `NAMEn:` | Titel, Untertitel, Kanalname        |
| Pico -> ESP32   | `MODE:SCENE` / `MODE:RELAY` | Aktiver Modus                            |
| Pico -> ESP32   | `SCENEn:<name>`          | Szenenname (leer = Szene inaktiv)           |
| Pico -> ESP32   | `STATEn:ON` / `STATEn:OFF` | Kanalzustand                              |
| Pico -> ESP32   | `ERRORn:ON/OFF` / `SERRORn:ON/OFF` | Rueckmeldefehler Kanal bzw. Szene   |

## Sessions

| Eigenschaft                | Wert                                                          |
| -------------------------- | ------------------------------------------------------------- |
| Cookie-Name                | `sid`                                                         |
| Token                      | 16 Zufallsbytes als 32-Zeichen-Hex                            |
| Cookie-Attribute           | `HttpOnly; SameSite=Strict; Path=/`                           |
| User-Lebensdauer           | 30 Minuten Sliding Window (`SESSION_LIFETIME_MS`)             |
| Admin-Persistent-Cookie    | gleiche `sid`, `Max-Age=31536000` (1 Jahr) - erlaubt Schalten nach Session-Timeout |
| Max. parallele Sessions    | 12 (aelteste wird verdraengt)                                 |
| Logout                     | Loescht Session und Persistent-Token                          |

## Persistenz

Konfiguration, Benutzer und API-Keys liegen in einem kompakten Block im Flash, redundant ueber bis zu vier Slots am Flash-Ende:

| Slot | Offset       | Adresse        |
| ---- | ------------ | -------------- |
| 1    | `0x0003F000` | `0x1003F000`   |
| 2    | `0x0007F000` | `0x1007F000`   |
| 3    | `0x000FF000` | `0x100FF000`   |
| 4    | letzter Sektor | abhaengig von `PICO_FLASH_SIZE_BYTES` |

Magic: `0x4F453558` ("OE5X"). Aktuelles Layout: `PERSIST_VERSION = 7`.

Persistierte Felder:

- 8 Relais-Zustaende
- Public-Access-Flag
- 8 Relais-Namen (je 32 Zeichen)
- Seitentitel und Untertitel/Header (je 64 Zeichen)
- Bis zu 8 Benutzer (`name`, SHA-256-Hex-`hash`, `role`)
- Bis zu 8 API-Keys (`key` 16 Hex-Zeichen, `comment` 64 Zeichen)
- Statische Netzwerkwerte: IP, Subnet Mask und Gateway
- Szenen, aktive Szene und Ausgangspolaritaet pro Relais
- Rueckmeldeaktivierung und Rueckmeldepolaritaet pro Relais
- Gemeinsame Rueckmeldefrist (`10` bis `10000 ms`, Standard `500 ms`)

### Verhalten beim Start

- Gueltiger Slot mit aktueller Version: laden.
- Bekannte Layouts `version = 1` bis `6`: einmalig auf v7 migrieren und neu schreiben. Neue Rueckmeldefelder erhalten dabei ihre sicheren Defaults (Ueberwachung aus, HIGH-aktiv, `500 ms`).
- Unbekannte Version oder ungueltiges Magic: `persist_write_locked = true`. Folgende `save_config()`-Aufrufe loggen, schreiben aber nichts. Schuetzt fremde Daten.
- Leerer Flash: Defaults nur im RAM, kein sofortiges Schreiben. Erste Aenderung schreibt alle Slots.

### Layoutaenderungen der persistenten Felder

- Felder nur **anhaengen**, nie umordnen oder schrumpfen.
- `PERSIST_VERSION` erhoehen.
- In `load_config()` einen weiteren `if (version == N)`-Migrationspfad ergaenzen.
- Den alten Layout-Struct als reine Lese-Referenz behalten.

## Sicherheitshinweise

- Passwoerter werden mit SHA-256 gehasht. Schwache oder kurze Passwoerter sind anfaellig fuer Brute-Force / Rainbow-Tables.
- HTTP ist unverschluesselt; Anmeldedaten und Cookies fliessen im Klartext durchs Netz. Den Server nur im vertrauenswuerdigen Netz betreiben.
- Default-Admin-Passwort `sw234` nach dem ersten Start aendern.
- API-Keys sind effektiv Bearer-Token; bei Kompromittierung ueber `/admin` loeschen.

## Troubleshooting

- **Pico erscheint nicht als `RP2350`**: BOOTSEL festhalten, dann USB einstecken; Kabel pruefen.
- **`Verbunden` springt staendig auf `Getrennt`**: Mehr als 6 SSE-Tabs offen oder Netzwerk verliert kurzzeitig Pakete. Tabs schliessen oder Pico neustarten.
- **Build bricht mit "ueberschreitet den ersten Persistenz-Slot" ab**: Firmware ist gewachsen. Ersten Slot in `persist_flash_offsets()` durch einen weiter hinten liegenden ersetzen.
- **Server schreibt Konfig nicht**: Im seriellen Log nach `Persistenz-Schreibschutz aktiv` suchen. Tritt auf, wenn fremde/kaputte Persistenzdaten erkannt wurden. Geraet einmalig mit definierter Konfiguration neu flashen oder die Slots manuell loeschen.
