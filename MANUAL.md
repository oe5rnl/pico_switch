# Benutzerhandbuch pico_switch

## 1. Überblick

`pico_switch` steuert bis zu acht Relais auf bis zu acht GPIO-Ausgängen. Die Bedienung ist auf zwei Arten möglich:

- über die Webseite mit einem PC, Tablet oder Smartphone im selben Netzwerk,
- direkt am ESP32-CYD-Touchdisplay.

**Grundkonzept:** Die sichtbaren Bedienelemente sind **Buttons** (1–8). Jeder Button
verweist auf einen **Eingang eines Relais** – er steuert also nicht mehr direkt eine
GPIO. **Relais** (1–8) werden getrennt konfiguriert und haben einen Typ:

- **1-fach:** 1 Button/Eingang → 1 Ausgang.
- **2-fach / 4-fach:** 2 bzw. 4 Buttons/Eingänge → 2 bzw. 4 Ausgänge, die sich
  **gegenseitig ausschließen** (immer genau ein Ausgang aktiv, wie ein Stufenschalter).

Jeder Relaisausgang statisch (fix aus oder ein) oder als **Impuls** arbeiten (kurzer Schaltpuls statt Dauer-EIN, Impulszeit
einstellbar). Änderungen werden zwischen Webseite, Pico und Display automatisch
übertragen. Die zuletzt gespeicherten Zustände und Einstellungen bleiben nach einem
Neustart erhalten.

> **Achtung:** Abhängig von der angeschlossenen Anlage können Relais Netzspannung oder andere gefährliche Lasten schalten. Änderungen an Verkabelung, Ausgangspolarität und Rückmeldeeingängen dürfen nur von fachkundigen Personen durchgeführt werden. Das System arbeitet mit 3,3 V an den Ein- und Ausgängen des ESP32 und des Pico. Diese Spannung darf nicht überschritten werden.

## 2. Schnellstart

1. Pico, W6300-Netzwerkanschluss und Display einschalten.
2. Warten, bis das Display statt `wait for init` den konfigurierten Titel anzeigt.
3. Nachdem eine IP-Adresse über DHCP zugewiesen wurde, wird diese unten am Display angezeigt. IP-Adresse in einem Webbrowser öffnen, zum Beispiel `http://192.168.88.188`. Die tatsächlich vergebene IP-Adresse hängt von der Netzwerkkonfiguration ab. Bei DHCP weist der Router die Adresse zu. Ohne erfolgreiche DHCP-Zuweisung verwendet das Gerät die konfigurierte statische Adresse.

4. Falls die Steuerung eine Anmeldung verlangt, oben rechts **Login** wählen.
5. Bei der ersten Inbetriebnahme mit folgenden Standarddaten anmelden:

   - Benutzername: `admin`
   - Passwort: `sw234`

6. Das Standardpasswort über das Benutzermenü oben rechts unter **Passwort ändern** ersetzen.


## 3. Bedienung über die Webseite

### 3.1 Kopfzeile und Verbindungsstatus

Die Kopfzeile enthält:

- den konfigurierten Seitentitel; ein Klick darauf öffnet die Hauptseite,
- **Konfig** zum Öffnen der Einstellungen,
- **Login** oder den Namen des angemeldeten Benutzers,
- den Status **Verbunden** oder **Getrennt**. **Verbunden** bedeutet, dass die Webseite laufend Statusmeldungen vom Pico erhält. Bei **Getrennt** sollte die Netzwerkverbindung geprüft und die Seite gegebenenfalls neu geladen werden.  

- Anzeige „Andere Benutzer:":
Im fixierten Seitenkopf jeder Webseite blendet die Pico-Oberfläche eine Statuszeile
mit dem Label „Andere Benutzer:" ein. Sie zeigt, welche **weiteren** Benutzer gerade
aktiv angemeldet sind – die eigene Anmeldung wird dabei nie aufgelistet.

 Pro aktivem Benutzer werden der Name und die verbleibende Sitzungsdauer in
 Minuten dargestellt, z. B. `Andere Benutzer: admin (30 min), gast (12 min)`. 

 **Keine weiteren Benutzer:** Ist niemand sonst angemeldet, erscheint `Andere Benutzer: -` (Tooltip: „Keine anderen aktiven Anmeldungen").  

 **Sichtbarkeit des Feldes:** Die Zeile wird nur angezeigt, wenn der Betrachter selbst angemeldet
  ist oder der öffentliche Zugriff (`public_access`) aktiviert ist. Anonyme Besucher
  ohne öffentlichen Zugriff sehen die Zeile nicht.

  **Live-Aktualisierung:** Der Browser fragt alle 5 Sekunden `/active_users` ab und
  aktualisiert die Liste sowie die Restzeiten automatisch, ohne Neuladen der Seite.  

So ist jederzeit erkennbar, ob gerade jemand anderes das System bedienen könnte.



### 3.2 Buttons direkt schalten

Im Button-Modus zeigt die Hauptseite die **aktivierten Buttons** mit Name, Nummer und
Zustand. Nur Buttons, die auf der Seite **Buttons** aktiviert sind, werden angezeigt.

- **ON** und eine grüne Markierung bedeuten: der zugeordnete Relais-Eingang ist aktiv.
- **OFF** und eine graue Markierung bedeuten: nicht aktiv.
- Ein Klick auf ein Feld schaltet den Button um. Bei einem 1-fach-Relais wird umgeschaltet
  (ein/aus); bei einem 2-/4-fach-Relais wird der zugehörige Ausgang angewählt und der
  vorher aktive Ausgang desselben Relais dadurch abgewählt.
- Eine rote Markierung bedeutet, dass die physische Rückmeldung nicht innerhalb der
  eingestellten Zeit zum Schaltzustand passt.

Die Anzeige wird bei Änderungen durch andere Benutzer oder durch das Touchdisplay automatisch aktualisiert.

### 3.3 Szenen verwenden

Eine Szene schaltet mehrere Relais mit einem einzigen Befehl. Wenn der Szenenmodus aktiviert ist, erscheint auf der Hauptseite des Webfensters zusätzlich der Bereich **Szenen**.

- Ein Klick auf eine Szene führt die hinterlegten Aktionen aus.
- Grün kennzeichnet die zuletzt aktivierte Szene, deren Zustand noch erfüllt ist.
- Rot kennzeichnet einen Rückmeldefehler bei der Ausführung dieser Szene.
- Ein kleiner roter Punkt oben rechts auf der aktiven Szene weist darauf hin, dass mindestens ein Relais nach der Aktivierung der Szene direkt (nicht über eine Szene) geschaltet wurde. Die angezeigten Zustände weichen damit vom definierten Szenenzustand ab. Der Punkt erlischt, sobald eine Szene erneut aktiviert wird.
- **1-fach-Relais werden in Szenen umgeschaltet** (Toggle wie ein Tastendruck): jede
  Ausführung der Szene wechselt den Zustand. Bei 2-/4-fach-Relais wählt die Szene den
  konfigurierten Ausgang an.
- Der darunterliegende Bereich **Buttons** bleibt auf der Webseite direkt bedienbar.

Eine Szene ist kein dauerhaft verriegelter Betriebszustand. Relais können nach dem Aufruf einer Szene weiterhin einzeln geschaltet werden.

### 3.4 Anmeldung und Abmeldung


Ohne öffentlichen Zugriff zeigt die Hauptseite erst nach der Anmeldung bedienbare Relais und Szenen. Bei aktiviertem öffentlichen Zugriff ist die Bedienung auch ohne Benutzerkonto möglich.

Nach der Anmeldung öffnet ein Klick beziehungsweise Überfahren des Benutzernamens das Menü:

- **Abmelden** beendet die Sitzung.
- **Passwort ändern** ändert das eigene Passwort. Dazu sind das aktuelle Passwort und das neue Passwort zweimal einzugeben.

Eine Anmeldung ist 30 Minuten gültig und wird bei weiterer Aktivität verlängert.

## 4. Bedienung am Touchdisplay

### 4.1 Anzeigen beim Start

Während das Display noch keine Verbindung zum Pico hat, steht in der Titelzeile `wait for init`. Sobald die serielle Verbindung hergestellt ist, lädt das Display Titel, IP-Adresse, Betriebsart, Namen und Zustände automatisch.

Kehrt die Anzeige später zu `wait for init` zurück, besteht keine Kommunikation mit dem Pico. In diesem Fall Stromversorgung und UART-Verbindung zwischen Display und Pico prüfen.

Die untere Zeile zeigt links die IP-Adresse des Webservers (bei verlorenem LAN-Link
eine entsprechende Fehlermeldung) und **unten rechts den aktiven Modus** („Buttons"
oder „Szenen").

### 4.2 Button-Modus

Im Button-Modus zeigt das Display die **aktivierten Buttons**. Die Größe passt sich
dynamisch an: bei 1–3 sichtbaren Buttons werden diese vergrößert dargestellt, ab 4
Buttons im 4-mal-2-Raster. Deaktivierte Buttons werden nicht angezeigt.

- Grün: Button ist **ON** (zugeordneter Relais-Eingang aktiv).
- Grau: Button ist **OFF**.
- Rot: Die konfigurierte physische Rückmeldung passt nicht zum Sollzustand.
- Antippen: schaltet den Button (1-fach: umschalten; 2-/4-fach: Ausgang anwählen).

Der Buttonname und der Zustand stehen direkt auf der Taste. Ein im Namen verwendetes Zeichen `|` erzeugt auf dem Display einen Zeilenumbruch.

### 4.3 Szenenmodus

Im Szenenmodus zeigt das Display nur aktivierte Szenen. Je nach Anzahl werden bis zu drei Szenen vergrößert dargestellt; bei mehr Szenen wird das 4-mal-2-Raster verwendet.

- Blau/Grau: Szene kann aktiviert werden.
- Grün: zuletzt aktivierte Szene.
- Rot: Rückmeldefehler bei der zuletzt ausgeführten Szene.
- Roter Punkt oben rechts auf der grünen Taste: Seit der letzten Szenenaktivierung wurde mindestens ein Relais direkt geschaltet. Der Pico-Zustand weicht damit vom Szenenzustand ab. Durch erneutes Aktivieren einer Szene verschwindet der Punkt.
- Antippen: Szene wird einmal ausgeführt.

Im Szenenmodus können einzelne Relais nicht am Display geschaltet werden. Die direkte Relaisbedienung bleibt jedoch auf der Webseite verfügbar. Auch in Szenennamen erzeugt `|` einen Zeilenumbruch auf dem Display.

### 4.4 Externe Taster

Jeder Relais-Ausgang hat einen fest zugeordneten **Eingang-GPIO** (siehe Abschnitt 6.1).
Dessen Funktion wird **zur Laufzeit** je Ausgang auf der Seite **Relais** gewählt:
**Rückmeldung** oder **Taster**. Ein als **Taster**
konfigurierter Eingang wird zwischen dem GPIO und GND angeschlossen; die interne
Pull-Up-Beschaltung ist dann aktiv (gedrückt = LOW).

- Ein Tastendruck löst **denselben Relais-Eingang** aus wie der zugehörige logische
  Button: bei 1-fach **umschalten** (Toggle), bei 2-/4-fach den betreffenden Ausgang anwählen.
- Ist der **Szenenmodus** aktiv, wirkt sich der Tastendruck über den Button auf die
  Relais aus wie im Web/Display.
- Die Taster arbeiten **parallel** zu Webseite und Touchdisplay.
- Die Eingänge sind **entprellt**. Die gemeinsame **Taster-Entprellzeit** wird auf der
  Seite **Relais** eingestellt (Standard 25 ms, zulässig 5 bis 2000 ms).

> **Hinweis:** Pro Ausgang kann der Eingang **entweder** Rückmeldung **oder** Taster sein
> (derselbe physische Pin). Beides gleichzeitig für denselben Ausgang ist nicht möglich.

## 5. Konfiguration durch Administratoren

Die administrativen Seiten sind über **Konfig** erreichbar und erfordern ein Konto mit der Rolle `admin`. Änderungen werden erst mit **Speichern** übernommen.

Beim **Speichern** prüft der Pico die Konfiguration auf Konsistenz:

- **Fehler** (rot) verhindern das Speichern und nennen die Ursache. Beispiele:
  Relais mit doppelt belegter Ausgangs-GPIO oder ohne GPIO; Button ohne/auf inaktives
  Relais oder auf einen nicht existierenden Eingang; zwei Buttons auf demselben
  Relais-Eingang; Szene mit mehreren „Ein" auf dasselbe Mehrfach-Relais.
- **Hinweise** (ebenfalls hervorgehoben) werden gespeichert, aber gemeldet. Sie melden
  insbesondere **nicht erfüllte Abhängigkeiten** zwischen Buttons, Relais und Szenen:
  - aktives Relais ohne zugeordneten Button; Mehrfach-Relais nur teilweise mit Buttons belegt;
  - beim Speichern der **Relais**-Seite: ein Button zeigt nun auf ein inaktiv gewordenes
    Relais oder auf einen durch den neuen Typ entfallenen Eingang;
  - beim Speichern der **Buttons**- oder **Szenen**-Seite: eine Szene nutzt einen Button,
    der jetzt inaktiv/nicht zugeordnet ist; eine Szene ohne Aktion.

Die horizontale Menüleiste ist auf allen Admin-Seiten gleich (Übersicht, Speichern,
Buttons, Relais, Szenen, Benutzer/API, Network, Abmelden); die aktuelle Seite ist rot
hervorgehoben.

### 5.1 Buttons (Seite „Buttons")

Auf der Seite **Buttons** werden die allgemeinen Werte und die Bedien-Buttons festgelegt:

- **Titel:** Name in der Kopfzeile der Webseite und auf dem Display.
- **Überschrift:** Text über den Bedienelementen der Hauptseite.
- **Öffentlicher Zugriff:** erlaubt die Bedienung ohne Anmeldung.
- Je **Button** (1–8):
  - **aktiv:** blendet den Button auf Webseite und Display ein/aus.
  - **Name:** Beschriftung des Buttons. Wird im Button-Mode auf dem Button angezeigt.
  - **Relais-Eingang:** Auswahl, welches Relais (und bei 2-/4-fach welcher Eingang)
    dieser Button ansteuert. Die Auswahl bietet nur aktivierte, gültige Relais an.

### 5.2 Relais (Seite „Relais")

Auf der Seite **Relais** werden die physischen Relais definiert (einklappbare Blöcke,
Kopfzeile bleibt sichtbar). Je Relais (1–8):

- **aktiv:** Relais verwenden.
- **Typ:** **1-fach** (1 Ausgang), **2-fach** oder **4-fach** (mehrere, gegenseitig
  ausschließende Ausgänge).
- **Name**, **Low aktiv** (kehrt die elektrische Ausgangslogik um),
  **Impuls** (kurzer Schaltpuls statt Dauer-EIN) und **Impulszeit** (100–2000 ms, Standard 300).
- Je Ausgang:
  - **Ausgangs-GPIO:** frei wählbar aus dem Pool GP2–GP9 (jeder Pin nur einmal).
  - **Eingang (abgeleitet):** der zum Ausgang gehörende Eingang-GPIO wird automatisch
    angezeigt (feste Paarung GP2↔GP10 … GP9↔GP28).
  - **Eingangsrolle:** **keine**, **Rückmeldung** oder **Taster** (siehe 4.4 und 5.3).
  - **LOW:** Rückmeldepegel für EIN ist LOW.
- Global: **Rückmeldezeit** (10–10000 ms) und **Taster-Entprellzeit** (5–2000 ms).

> **Impuls-Verhalten:** Steht ein Ausgang auf EIN, gibt er bei aktiviertem **Impuls**
> beim Einschalten des Geräts **einen Impuls** aus (danach Leitung idle, Anzeige bleibt
> EIN); ohne Impuls wird er statisch gehalten. Ein Sicherheits-Timer stellt sicher, dass
> ein Impuls-Ausgang **nie länger als die Impulszeit** aktiv ist.

> **Wichtig:** Eine Änderung von **Low aktiv** oder der GPIO-Zuordnung wird beim Speichern
> sofort auf die Ausgänge angewendet. Vorher sicherstellen, dass die Einstellung zur
> angeschlossenen Relaisplatine passt.

### 5.3 Rückmeldung der Relais

Die Rückmeldung prüft, ob ein Relais den vom System ausgegebenen Zustand physisch tatsächlich erreicht hat. Dazu besitzt jeder Kanal einen eigenen Rückmeldeeingang (siehe Abschnitt 6.1), an den zum Beispiel ein Hilfskontakt des Relais oder eine Stromerkennung angeschlossen wird.

Funktionsweise pro Kanal:

1. Beim Schalten gibt der Pico den Sollzustand an den Relaisausgang aus.
2. Ist die Rückmeldung für den Kanal aktiviert, liest der Pico den zugehörigen Rückmeldeeingang.
3. Der gemessene Pegel wird mit dem Sollzustand verglichen. Bei **Rückm. LOW** gilt LOW als eingeschaltet, sonst HIGH.
4. Stimmt die Rückmeldung sofort mit dem Sollzustand überein, gilt der Kanal als in Ordnung.
5. Andernfalls startet eine Frist (**Rückmeldezeit**), damit das Relais mechanisch schalten kann.
6. Trifft die passende Rückmeldung innerhalb dieser Frist ein, gilt der Kanal weiterhin als in Ordnung.
7. Läuft die Frist ohne Übereinstimmung ab, wird ein Rückmeldefehler gesetzt.

Wichtige Eigenschaften:

- Die Rückmeldung ändert **nie** den Sollzustand, sondern meldet nur eine Abweichung.
- Ein Fehler ist **selbstheilend**: Erreicht das Relais den Sollzustand später doch noch, erlischt der Fehler automatisch.
- Die Eingangsrolle **Rückmeldung** und die Polarität **LOW** werden je Relais-Ausgang eingestellt (Seite **Relais**); die **Rückmeldezeit** gilt gemeinsam für alle (10 bis 10000 ms).
- Ein Rückmeldefehler wird auf der Webseite und am Display rot angezeigt. Wird der Fehler durch eine Szene ausgelöst, erscheint zusätzlich die betreffende Szene als fehlerhaft.

> **Hinweis:** Ein Rückmeldefehler bedeutet, dass der tatsächliche Zustand der Anlage vom angezeigten Sollzustand abweichen kann. In diesem Fall die betroffene Last, Verkabelung und Rückmeldepolarität prüfen.

### 5.4 Szenen konfigurieren

Über **Szenen** stehen acht Szenen zur Verfügung (einklappbare Blöcke, Kopfzeile bleibt
sichtbar). Für jede Szene können Administratoren:

1. die Szene mit **aktiv** ein- oder ausblenden,
2. einen Namen vergeben der am Szenenbutton angezeigt wird,
3. je **Button** eine Aktion auswählen:

   - Für Buttons eines **1-fach-Relais**: **Umschalten** (Toggle bei jeder Ausführung)
     oder **—** (unverändert lassen).
   - Für Buttons eines **2-/4-fach-Relais**: **Ein** (diesen Ausgang anwählen),
     **Aus** (bedeutungslos, wird übersprungen) oder **—** (unverändert).

Die Option **Szenen-Modus aktiv** bestimmt, ob die Haupttasten Szenen oder direkt die
Buttons bedienen. Nach Änderungen **Speichern** wählen. Deaktivierte Szenen erscheinen
weder im Szenenbereich der Webseite noch auf dem Display.

### 5.5 Benutzer und API-Keys

Unter **Konfig** > **Benutzer/API** können Administratoren:

- aktive Anmeldungen und deren Restlaufzeit einsehen,
- Benutzer anlegen und ihnen die Rolle `user` oder `admin` geben,
- Rollen und Passwörter vorhandener Benutzer ändern,
- Benutzer löschen,
- API-Keys mit einem Kommentar erzeugen, bearbeiten oder löschen.

Ein neues Passwort muss mindestens vier Zeichen lang sein. Der eigene Benutzer kann nicht gelöscht, die eigene Admin-Rolle nicht entfernt und der letzte Administrator nicht herabgestuft werden.

API-Keys sind für externe Programme vorgesehen und sollten wie Passwörter geschützt werden. Benutzer tragen einen API-Key im HTTP-Header `X-API-Key` ein.

### 5.6 Netzwerk

Unter **Network** werden der aktuelle Modus, die MAC-Adresse, IP-Adresse, Subnetzmaske und das Gateway angezeigt. Dort lassen sich außerdem die gespeicherte statische IP-Adresse, Subnetzmaske und das Gateway ändern.

Ob beim Start DHCP oder die statische Konfiguration verwendet wird, bestimmt der Hardwareeingang GP15 am Pico:

- GP15 HIGH oder offen: DHCP,
- GP15 LOW: statische Konfiguration.

Gespeicherte Netzwerkwerte werden erst beim nächsten Start im statischen Modus wirksam. Falsche Werte können die Webseite unerreichbar machen. Falls dieser Fall eintritt, über GP15 auf DHCP schalten. Vor einer Änderung sollten IP-Adresse, Subnetzmaske und Gateway mit der zuständigen Netzwerkadministration abgestimmt werden.

## 6. GPIO-Belegung

### 6.1 Raspberry Pi Pico (W6300-EVB-Pico2)

| GPIO am Pico | Funktion |
|---|---|
| GP0 | UART0 TX zum Display (Pico -> ESP32 GPIO3 RX) |
| GP1 | UART0 RX vom Display (Pico <- ESP32 GPIO1 TX) |
| GP2 bis GP9 | Ausgangs-GPIO-Pool (je Relais-Ausgang frei wählbar) |
| GP10, GP11, GP12, GP13, GP14, GP26, GP27, GP28 | Eingangs-Pool: fest gepaart zum Ausgang (GP2↔GP10 … GP9↔GP28), je Ausgang **Rückmeldung ODER Taster** (Laufzeit, Seite **Relais**) |
| GP15 | Netzwerkmodus-Bootstrap: HIGH/offen = DHCP, LOW = statische IP |
| GP16 bis GP22 | Reserviert für W6300 (QSPI-LAN-Interface) |

### 6.2 ESP32-CYD Display

| GPIO am ESP32 | Funktion |
|---|---|
| GPIO1 | UART TX zum Pico GP1 |
| GPIO3 | UART RX vom Pico GP0 |

Hinweise:

- UART ist 3,3 V Logik und muss gekreuzt verdrahtet werden (TX auf RX).
- GND von Pico und Display muss verbunden sein.
- GP16 bis GP22 am Pico nicht für eigene Verdrahtung nutzen, da diese Pins für den W6300 benötigt werden.

## 7. Fehlerbehebung

| Anzeige oder Problem | Bedeutung und Maßnahme |
|---|---|
| Display zeigt `wait for init` | Keine Kommunikation mit dem Pico. Stromversorgung, gemeinsame Masse sowie die gekreuzten TX/RX-Leitungen prüfen. |
| Webseite zeigt **Getrennt** | Browser empfängt keine Live-Daten. Netzwerkverbindung prüfen und Seite neu laden. |
| Rotes Relaisfeld | Physische Rückmeldung stimmt nicht rechtzeitig mit dem Sollzustand überein. Relais, Last, Rückmeldekontakt, Polarität und Rückmeldezeit prüfen. |
| Rote Szene | Mindestens eine relevante Rückmeldung der zuletzt ausgeführten Szene ist fehlerhaft. Betroffene Relais einzeln kontrollieren. |
| Roter Punkt auf aktiver Szene | Ein oder mehrere Relais wurden nach der letzten Szenenaktivierung direkt geschaltet. Die Relaiszustände stimmen nicht mehr mit dem Szenenzustand überein. Eine Szene aktivieren, um den definierten Zustand wiederherzustellen. |
| Webseite nicht erreichbar | Angezeigte Display-IP, LAN-Kabel, DHCP-Zuweisung und statische Netzwerkeinstellungen prüfen. |
| Schalten ohne Anmeldung nicht möglich | Anmelden oder den öffentlichen Zugriff durch einen Administrator aktivieren lassen. |
| Szene fehlt auf Display/Webseite | In der Szenenkonfiguration prüfen, ob die Szene aktiviert und gespeichert wurde. |
| Änderung erscheint nicht sofort | Verbindungsstatus prüfen. Bei Konfigurationsänderungen sicherstellen, dass **Speichern** bestätigt wurde. |

## 8. Sicherheitshinweise

- Das Standardpasswort unmittelbar nach der ersten Anmeldung ändern.
- Öffentlichen Zugriff nur in vertrauenswürdigen, abgeschotteten Netzwerken aktivieren.
- API-Keys nicht in frei zugänglichen Dateien oder Nachrichten veröffentlichen.
- Vor Änderungen an Polarität, Rückmeldeeingängen oder Netzwerkparametern die bestehende Konfiguration dokumentieren.
- Bei einer roten Fehleranzeige nicht allein auf den angezeigten Sollzustand vertrauen, sondern den tatsächlichen Zustand der angeschlossenen Anlage prüfen.
