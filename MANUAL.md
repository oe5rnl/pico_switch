# Benutzerhandbuch pico_switch

## 1. Überblick

`pico_switch` steuert bis zu acht Relais. Die Bedienung ist auf zwei Arten möglich:

- über die Webseite mit einem PC, Tablet oder Smartphone im selben Netzwerk,
- direkt am ESP32-CYD-Touchdisplay.

Änderungen werden zwischen Webseite, Pico und Display automatisch übertragen. Die zuletzt gespeicherten Relaiszustände und Einstellungen bleiben nach einem Neustart erhalten.

> **Achtung:** Abhängig von der angeschlossenen Anlage können Relais, Netzspannung oder andere gefährliche Lasten schalten. Änderungen an Verkabelung, Ausgangspolarität und Rückmeldeeingängen dürfen nur von fachkundigen Personen durchgeführt werden. Das System arbeitet mit 3,3V Spannung an den Ein/Ausgängen des ESP und Pico. Diese Spannungen darf nicht überschritten werden.

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

- Anzeige „Andere Benutzer:"
Im fixierten Seitenkopf jeder Web-Seite blendet die Pico-Oberflaeche eine Statuszeile
mit dem Label „Andere Benutzer:" ein. Sie zeigt, welche **weiteren** Benutzer gerade
aktiv angemeldet sind – die eigene Anmeldung wird dabei nie aufgelistet.

 Pro aktivem Benutzer wird Name und verbleibende Session-Restzeit in
 Minuten dargestellt, z. B. `Andere Benutzer: admin (30 min), gast (12 min)`. 

 **Keine weiteren Benutzer:** Ist niemand sonst angemeldet, erscheint `Andere Benutzer: -` (Tooltip: „Keine anderen aktiven Anmeldungen").  

 **Sichtbarkeit des Feldes:** Die Zeile wird nur angezeigt, wenn der Betrachter selbst angemeldet
  ist oder der oeffentliche Zugriff (`public_access`) aktiviert ist. Anonyme Besucher
  ohne oeffentlichen Zugriff sehen die Zeile nicht.  

  **Live-Aktualisierung:** Der Browser fragt alle 5 Sekunden `/active_users` ab und
  aktualisiert die Liste sowie die Restzeiten automatisch, ohne Neuladen der Seite.  

So ist jederzeit erkennbar, ob gerade jemand anderes das System bedienen koennte.



### 3.2 Relais direkt schalten

Im Relaismodus zeigt die Hauptseite acht Relaisfelder mit Name, Nummer und Zustand.

- **ON** und eine grüne Markierung bedeuten: Relais eingeschaltet.
- **OFF** und eine graue Markierung bedeuten: Relais ausgeschaltet.
- Ein Klick auf ein Relaisfeld schaltet zwischen **ON** und **OFF** um.
- Eine rote Markierung bedeutet, dass die physische Rückmeldung nicht innerhalb der eingestellten Zeit zum Schaltzustand passt.

Die Anzeige wird bei Änderungen durch andere Benutzer oder durch das Touchdisplay automatisch aktualisiert.

### 3.3 Szenen verwenden

Eine Szene schaltet mehrere Relais mit einem einzigen Befehl. Wenn der Szenenmodus aktiviert ist, erscheint auf der Hauptseite des Webfensters zusätzlich der Bereich **Szenen**.

- Ein Klick auf eine Szene führt die hinterlegten Aktionen aus.
- Grün kennzeichnet die zuletzt aktivierte Szene.
- Rot kennzeichnet einen Rückmeldefehler bei der Ausführung dieser Szene.
- Ein kleiner roter Punkt oben rechts auf der aktiven Szene weist darauf hin, dass mindestens ein Relais nach der Aktivierung der Szene direkt (nicht über eine Szene) geschaltet wurde. Die angezeigten Relaiszustände weichen damit vom definierten Szenenzustand ab. Der Punkt erlischt, sobald eine Szene erneut aktiviert wird.
- Der darunterliegende Bereich **Relais** bleibt auf der Webseite direkt bedienbar.

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

Die untere Zeile zeigt die IP-Adresse des Webservers. Bei einem verlorenen LAN-Link wird dort eine entsprechende Fehlermeldung angezeigt.

### 4.2 Relaismodus

Im Relaismodus werden alle acht Kanäle in einem 4-mal-2-Raster angezeigt.

- Grün: Relais ist **ON**.
- Grau: Relais ist **OFF**.
- Rot: Die konfigurierte physische Rückmeldung passt nicht zum Sollzustand.
- Antippen: Relaiszustand wird umgeschaltet.

Der Relaisname und der Zustand stehen direkt auf der Taste. Ein im Namen verwendetes Zeichen `|` erzeugt auf dem Display einen Zeilenumbruch.

### 4.3 Szenenmodus

Im Szenenmodus zeigt das Display nur aktivierte Szenen. Je nach Anzahl werden bis zu drei Szenen vergrößert dargestellt; bei mehr Szenen wird das 4-mal-2-Raster verwendet.

- Blau/Grau: Szene kann aktiviert werden.
- Grün: zuletzt aktivierte Szene.
- Rot: Rückmeldefehler bei der zuletzt ausgeführten Szene.
- Roter Punkt oben rechts auf der grünen Taste: Seit der letzten Szenenaktivierung wurde mindestens ein Relais direkt geschaltet. Der Pico-Zustand weicht damit vom Szenenzustand ab. Durch erneutes Aktivieren einer Szene verschwindet der Punkt.
- Antippen: Szene wird einmal ausgeführt.

Im Szenenmodus können einzelne Relais nicht am Display geschaltet werden. Die direkte Relaisbedienung bleibt jedoch auf der Webseite verfügbar. Auch in Szenennamen erzeugt `|` einen Zeilenumbruch auf dem Display.

### 4.4 Externe Taster (nur im Taster-Modus)

Ist die Firmware im **Taster-Modus** erstellt (Compilerschalter `INPUT_MODE=taster`, Standard), dienen die acht Eingänge GP10–GP28 als Anschluss für mechanische Taster. Ein Taster wird zwischen dem jeweiligen Eingang und GND angeschlossen; die interne Pull-Up-Beschaltung ist fest aktiv.

- Jeder Taster ist einem Relais fest zugeordnet (Taster an GP10 → Relais 1 usw.).
- Ein Tastendruck **schaltet um** (Toggle): einmal drücken schaltet ein, erneutes Drücken schaltet aus.
- Ist der **Szenenmodus** aktiv, löst der Taster stattdessen die zugehörige Szene aus (Taster 1 → Szene 1 usw.), sofern diese aktiviert ist.
- Die Taster arbeiten **parallel** zu Webseite und Touchdisplay; alle Bedienwege bleiben gleichzeitig nutzbar.
- Die Eingänge sind **entprellt**. Die gemeinsame **Taster-Entprellzeit** wird auf der Konfigurationsseite eingestellt (Standard 25 ms, zulässig 5 bis 2000 ms).
- Auf der Konfigurationsseite zeigt eine kleine **LED** je Kanal, ob der zugehörige Taster gerade gedrückt ist (zur Verdrahtungskontrolle).

> **Hinweis:** In diesem Modus steht die physische Relais-Rückmeldung (Abschnitt 5.2) nicht zur Verfügung, da dieselben Eingänge als Taster verwendet werden. Wird die Rückmeldung benötigt, muss die Firmware im Modus `rueckm` erstellt werden.

## 5. Konfiguration durch Administratoren

Die administrativen Seiten sind über **Konfig** erreichbar und erfordern ein Konto mit der Rolle `admin`. Änderungen werden erst mit **Speichern** übernommen.

### 5.1 Allgemeine Konfiguration

Auf der Seite **Konfiguration** können folgende Werte geändert werden:

- **Titel:** Name in der Kopfzeile der Webseite und auf dem Display.
- **Überschrift:** Text über den Bedienelementen der Hauptseite.
- **Relaisnamen:** Bezeichnungen der acht Kanäle.
- **Öffentlicher Zugriff:** erlaubt die Bedienung ohne Anmeldung.
- **Low aktiv:** kehrt für das betreffende Relais die elektrische Ausgangslogik um.
- **Rückmeldung:** aktiviert die Prüfung des zugehörigen physischen Rückmeldeeingangs.
- **Rückm. LOW:** legt fest, dass der aktive Rückmeldepegel LOW ist.
- **Rückmeldezeit:** Zeit, innerhalb der die physische Rückmeldung nach einem Schaltvorgang eintreffen muss; zulässig sind 10 bis 10000 ms.

> **Eingangsmodus (Taster oder Rückmeldung):** Die Funktion der acht Eingänge
> (GP10–GP28) wird beim Erstellen der Firmware festgelegt (Compilerschalter
> `INPUT_MODE`, Standard: Taster). Im **Taster-Modus** entfallen die Felder
> **Rückmeldung** und **Rückm. LOW**; jede Relaiszeile zeigt stattdessen den Text
> **„Taster GPxx"** und eine kleine **LED**, die aufleuchtet, solange der Taster
> gedrückt ist. Statt **Rückmeldezeit** erscheint dann **Taster-Entprellzeit**
> (gemeinsam für alle Taster, Standard 25 ms, zulässig 5 bis 2000 ms). Näheres
> siehe Abschnitt 4.4.

> **Wichtig:** Eine Änderung von **Low aktiv** wird sofort auf den Relaisausgang angewendet. Vor dem Speichern muss sichergestellt sein, dass die Einstellung zur angeschlossenen Relaisplatine passt.

### 5.2 Rückmeldung der Relais

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
- Aktivierung (**Rückmeldung**) und Polarität (**Rückm. LOW**) gelten je Kanal, die **Rückmeldezeit** gilt gemeinsam für alle Kanäle (10 bis 10000 ms).
- Ein Rückmeldefehler wird auf der Webseite und am Display rot angezeigt. Wird der Fehler durch eine Szene ausgelöst, erscheint zusätzlich die betreffende Szene als fehlerhaft.

> **Hinweis:** Ein Rückmeldefehler bedeutet, dass der tatsächliche Zustand der Anlage vom angezeigten Sollzustand abweichen kann. In diesem Fall die betroffene Last, Verkabelung und Rückmeldepolarität prüfen.

### 5.3 Szenen konfigurieren

Über **Konfig** > **Szenen** stehen acht Szenen zur Verfügung.

Für jede Szene können Administratoren:

1. die Szene mit **aktiv** ein- oder ausblenden,
2. einen Namen vergeben,
3. für jedes Relais eine Aktion auswählen:

   - **Ein:** Relais einschalten,
   - **Aus:** Relais ausschalten,
   - **—:** Relais unverändert lassen.

Die Option **Szenen-Modus aktiv** bestimmt, ob die Haupttasten Szenen oder direkt die Relais bedienen. Nach Änderungen **Speichern** wählen. Deaktivierte Szenen erscheinen weder im Szenenbereich der Webseite noch auf dem Display.

### 5.4 Benutzer und API-Keys

Unter **Konfig** > **Benutzer/API** können Administratoren:

- aktive Anmeldungen und deren Restlaufzeit einsehen,
- Benutzer anlegen und ihnen die Rolle `user` oder `admin` geben,
- Rollen und Passwörter vorhandener Benutzer ändern,
- Benutzer löschen,
- API-Keys mit einem Kommentar erzeugen, bearbeiten oder löschen.

Ein neues Passwort muss mindestens vier Zeichen lang sein. Der eigene Benutzer kann nicht gelöscht, die eigene Admin-Rolle nicht entfernt und der letzte Administrator nicht herabgestuft werden.

API-Keys sind für externe Programme vorgesehen und sollten wie Passwörter geschützt werden. Benutzer tragen einen API-Key im HTTP-Header `X-API-Key` ein.

### 5.5 Netzwerk

Unter **Konfig** > **Network** werden der aktuelle Modus, die MAC-Adresse, IP-Adresse, Subnetzmaske und das Gateway angezeigt. Dort lassen sich außerdem die gespeicherte statische IP-Adresse, Subnetzmaske und das Gateway ändern.

Ob beim Start DHCP oder die statische Konfiguration verwendet wird, bestimmt der Hardwareeingang GP15 am Pico:

- GP15 HIGH oder offen: DHCP,
- GP15 LOW: statische Konfiguration.

Gespeicherte Netzwerkwerte werden erst beim nächsten Start im statischen Modus wirksam. Falsche Werte können die Webseite unerreichbar machen. Falls dieser Fall eintritt -> über GP15 auf DHCP schalten. Vor einer Änderung sollten IP-Adresse, Subnetzmaske und Gateway mit der zuständigen Netzwerkadministration abgestimmt werden.

## 6. GPIO-Belegung

### 6.1 Raspberry Pi Pico (W6300-EVB-Pico2)

| GPIO am Pico | Funktion |
|---|---|
| GP0 | UART0 TX zum Display (Pico -> ESP32 GPIO3 RX) |
| GP1 | UART0 RX vom Display (Pico <- ESP32 GPIO1 TX) |
| GP2 bis GP9 | Relaisausgaenge 1 bis 8 |
| GP10, GP11, GP12, GP13, GP14, GP26, GP27, GP28 | Rueckmeldeeingaenge ODER Taster-Eingaenge fuer Relais 1 bis 8 (je nach Compilerschalter INPUT_MODE) |
| GP15 | Netzwerkmodus-Bootstrap: HIGH/offen = DHCP, LOW = statische IP |
| GP16 bis GP22 | Reserviert fuer W6300 (QSPI LAN-Interface) |

### 6.2 ESP32-CYD Display

| GPIO am ESP32 | Funktion |
|---|---|
| GPIO1 | UART TX zum Pico GP1 |
| GPIO3 | UART RX vom Pico GP0 |

Hinweise:

- UART ist 3,3 V Logik und muss gekreuzt verdrahtet werden (TX auf RX).
- GND von Pico und Display muss verbunden sein.
- GP16 bis GP22 am Pico nicht fuer eigene Verdrahtung nutzen, da diese Pins fuer den W6300 benoetigt werden.

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
