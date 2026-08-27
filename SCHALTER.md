# Spezifikation: 3-stufige Struktur Button → Schalter → GPIO

Status: **Design final, noch nicht implementiert.**
Betroffen: `pico/switch_server/src/relay_server.cpp`, `esp32/src/main.cpp`, `CLAUDE.md`.

Diese Datei beschreibt den geplanten Umbau der bisher **2-stufigen, 1:1**-Struktur
(8 Buttons steuern direkt 8 Relais/GPIO) auf eine **3-stufige** Struktur mit einer
Zwischenebene „Schalter".

---

## 1. Hierarchie

```
Button (max 8)  --1..8-->  Schalter (max 8)  --1..8-->  GPIO (max 8)
  Display/Web              Typ + eigener State        Ausgang + fester Rückmelde-Eingang
```

- **Button** (max. 8): Bedienelement auf Display und Web. Treibt 1..8 **Schalter**.
- **Schalter** (max. 8): logisches Schaltobjekt mit **Typ** und **eigenem Zustand**.
  Treibt 1..8 **GPIO**.
- **GPIO** (max. 8 gesamt): ein physisches Ausgang/Eingang-**Paar** wie der heutige
  Kanal — Ausgangspin (GP2–9) plus **fest zugeordneter** Rückmelde-/Taster-Eingang
  (GP10–14, 26–28). Der Rückmelde-Eingang gehört fix zum GPIO und zählt **nicht**
  gegen das 8er-Budget.

### Grenzen

| Element | Max | Speicherung |
|---|---|---|
| Button | 8 | je Button 8-Bit-Maske der zugeordneten Schalter |
| Schalter | 8 | je Schalter 8-Bit-Maske der zugeordneten GPIO + Typ + State |
| GPIO | 8 (gesamt) | je GPIO Verhalten (siehe unten) |

Alle Zuordnungen als kompakte Index-Masken → passt problemlos in den 4-KB-Flash-Sektor.

---

## 2. GPIO-Verhalten (wie bisher, je GPIO einstellbar)

- **low-aktiv** (`active_low`)
- **Impuls** (`impulse`) + **Impulszeit** (`impulse_ms`, 100–2000 ms)
- **Rückmeldung** (`feedback_enabled`) + **Rückmeldung low** (`feedback_active_low`)
- Der Rückmelde-/Taster-Eingang ist dem GPIO **fest** zugeordnet (kein extra Pin-Budget).

---

## 3. Schalter-Typen

- **toggle**: statischer Ausgang, schaltet ein/aus (wie bisheriges Relais).
- **bistabil**: wird mit **einem** Impulsausgang geschaltet (jeder Puls toggelt),
  der Zustand wird **im Schalter gespeichert**. **Kein** getrennter Set/Reset-Doppelausgang.
  - Rückmeldung ist **optional**: Ist für das zugehörige GPIO eine Rückmeldung
    definiert → **physisch prüfen**. Sonst gilt der **gespeicherte Software-Zustand**
    des Schalters.

---

## 4. Button → Schalter (Semantik)

- Button **EIN** = **alle** zugeordneten Schalter EIN.
- Button **AUS** = **alle** zugeordneten Schalter AUS.

### Angezeigter Button-Zustand (Aggregat der Schalter)

| Bedingung | Anzeige |
|---|---|
| alle Schalter ein | **grün** (EIN) |
| alle Schalter aus | **grau** (AUS) |
| gemischt (teils ein, teils aus) | **blau** (unbestimmt/„changed") |
| Rückmeldung ausstehend | **gelb** |
| Rückmeldefehler | **rot** |

---

## 5. Rückmeldung & Fehler-Aggregation

- Bewertung je GPIO wie bisher (aktivierbar/polarisierbar, gemeinsame Rückmeldezeit).
- **Aggregation:** GPIO → Schalter → Button. Ein einziges „nicht ok" ⇒ Fehler auf
  der jeweils höheren Ebene (Summe).
- Bei **Impuls-GPIO** wird zuerst die **Impulszeit** abgewartet, danach die
  Rückmeldung am End-Zustand bewertet.
- **Während der Wartezeit** auf Rückmeldungen wird der Button **gelb** dargestellt.

---

## 6. Physische Taster (`INPUT_MODE=taster`)

- Die physischen Taster (GP10–28) treiben **Schalter** (nicht Button, nicht GPIO direkt).

---

## 7. Szenen

- Der bisherige **Szenen-Modus entfällt komplett**: `scene_mode`, Szenen-Struktur,
  `/scenes`-Endpunkt, ESP-Protokoll `MODE:SCENE`/`SCENEn`/`SERROR`/`ASCENE`/`SDIRTY`
  werden entfernt.

---

## 8. Persistenz

- Neue `PERSIST_VERSION = 9` (aktuell 8).
- **Migration von V8:** Default-**1:1-Mapping** anlegen — Button *i* → Schalter *i*
  → GPIO *i*, damit Bestandsanlagen unverändert weiterlaufen. GPIO-Verhalten
  (active_low, impulse, feedback) wird aus den bisherigen Relais-Feldern übernommen.
- `cmake/check_persist_overlap.cmake` erneut prüfen (Struktur bleibt klein).

---

## 9. ESP-Protokoll (Änderungen)

- **Bleibt (button-bezogen):** `STATEn:ON/OFF`, `ERRORn:ON/OFF`, `NAMEn`, `TITLE`, `IP`.
- **Neu:**
  - `PENDINGn:ON/OFF` — Button *n* wartet auf Rückmeldung (**gelb**).
  - `CHANGEDn:ON/OFF` — Button *n* im Mischzustand (**blau**).
- **Entfernt:** `MODE:SCENE`/`MODE:RELAY`, `SCENEn`, `SERRORn`, `ASCENE`, `SDIRTY`
  sowie `SCENEn:GO`.
- `esp32/src/main.cpp`: Button-Hintergrund um **gelb** (pending) und **blau**
  (changed) erweitern.

---

## 10. Web-UI (Änderungen)

- **/config**: Editoren für
  - Button → Schalter-Zuordnung (Maske),
  - Schalter-Typ (toggle/bistabil) + Schalter → GPIO-Zuordnung (Maske),
  - GPIO-Verhalten (low-aktiv, Impuls + Impulszeit, Rückmeldung + Rückmeldung low).
- **/state**: pro Button `state` (ein/aus), `pending` (gelb), `changed` (blau),
  `error` (rot); optional Schalter-Detailarray.
- **/scenes** entfällt.
