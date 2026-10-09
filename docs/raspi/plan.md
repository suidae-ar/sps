# Plan: Raspberry Pi als E/A-Baugruppe für S5Sim

## 1. Ziel

- Echte 24-V-Ein- und -Ausgänge an einem Raspberry Pi, angesteuert von S5Sim.
- S5Sim läuft auf dem Mac (oder auf demselben Pi mit Adresse `127.0.0.1`).
- Die wenigen Pins des Pi werden sichtbar gemacht: Was nicht an einem echten Pin hängt, existiert nur
  auf dem Bildschirm – darauf weist S5Sim hin.
- Sicher im Fehlerfall: Bricht die Verbindung ab oder hängt die Software, schalten die Ausgänge ab.

## 2. Bausteine

### 2.1 `s5io` – Dienst auf dem Pi

- Kleines C++-Programm ohne Oberfläche, als systemd-Dienst gestartet.
- GPIO-Zugriff über **libgpiod 2** (aktuelles Raspberry Pi OS). Die Pins werden über ihre Namen
  (`GPIO17` …) gesucht, nicht über die Chipnummer – so läuft dasselbe Programm auf Pi 4, Pi 5 und Zero
  (beim Pi 5 hängen die Pins am RP1-Chip, dessen Nummer je nach Kernel wechselt).
- Je Eingang einstellbar: interner Pull-up/-down, Invertierung, Entprellzeit.
- **Watchdog:** Kommt 500 ms lang keine Nachricht von S5Sim, werden alle Ausgänge abgeschaltet.
  Beim Beenden des Dienstes ebenso.
- Später austauschbare **Treiber**: direkte GPIO (Stufe 1), Multiplex-Eingänge mit 74HC151 (alte
  Platine), I²C-Portbausteine MCP23017 (bis 128 E/A).

### 2.2 Baugruppe „Raspberry-Pi-E/A“ in S5Sim

- Wird im Baugruppenträger gesteckt wie DE/DA, hat aber Ein- **und** Ausgänge (z.B. EB 0 und AB 0).
- Einstellungen per Rechtsklick: Adresse des Pi (Name oder IP, Port), Byte-Adressen, **Pinbelegung je
  Bit** (GPIO-Nummer oder „nicht belegt“), invertiert, Entprellzeit.
- Darstellung: Pi-typische grüne Platine, je Bit LED und Beschriftung „GPIO 17“; nicht belegte Bits
  ausgegraut; Verbindungs-LED (grün = verbunden, rot blinkend = getrennt).
- Eingänge der Pi-Baugruppe sind auf dem Bildschirm nicht schaltbar – sie kommen von der Hardware.

### 2.3 Hinweise auf fehlende Hardware

Steckt eine Pi-Baugruppe im Träger, prüft S5Sim beim Übersetzen die im Programm verwendeten Ein- und
Ausgänge:

- Operanden **ohne echten Pin** (nur virtuelle Karte oder gar keine Karte): Warnung, z.B.
  *„E 2.3 und A 1.4 gibt es nur auf dem Bildschirm, nicht am Raspberry Pi.“*
- Bits der Pi-Baugruppe ohne Pin-Zuordnung, die das Programm benutzt: Warnung *„E 0.6 ist auf der
  Pi-Baugruppe nicht belegt“*.
- Doppelt vergebene Pins oder Pins mit Sonderfunktion (I²C, UART, SPI): Fehler bzw. Warnung bereits beim
  Einstellen der Baugruppe.

Es sind bewusst **Warnungen**, keine Fehler: Gemischt mit virtuellen Karten zu testen bleibt erlaubt.

### 2.4 Verhalten bei Störungen

| Ereignis | Pi (`s5io`) | S5Sim |
|---|---|---|
| Verbindung weg | Watchdog schaltet alle Ausgänge ab | Pi-Eingänge = 0, Verbindungs-LED rot, STÖRUNG-LED der CPU, Meldung; automatischer Neuaufbau |
| CPU in STOP | Ausgänge aus (S5Sim sendet 0) | wie bisher |
| `s5io` startet | alle Ausgänge aus, bis S5Sim etwas anderes sendet | – |
| Pi startet neu | Pins sind Eingänge (Ausgänge der Platine aus, siehe Pinbelegung) | – |

Die CPU wird **nicht** automatisch in STOP versetzt – das entscheidet der Bediener.

## 3. Protokoll (Entwurf)

Textzeilen über TCP, Standardport **2511** (in Anlehnung an AS511), UTF-8, Zeilenende `\n`. Text statt
Binärformat, damit man zur Fehlersuche mit `nc pi4.local 2511` mitlesen und von Hand senden kann.

```
S5Sim → s5io   HELLO S5SIM 1
s5io → S5Sim   HELLO S5IO 1 MODEL="Raspberry Pi 4 Model B Rev 1.5"
s5io → S5Sim   PINS 4,5,6,12,13,16,17,18,19,20,21,22,23,24,25,26,27      (frei verwendbare GPIOs)

S5Sim → s5io   IN  0 GPIO=5  BIAS=DOWN INVERT=0 DEBOUNCE=10             (Kanal 0 = E 0.0)
S5Sim → s5io   IN  1 GPIO=6  BIAS=DOWN INVERT=0 DEBOUNCE=10
…
S5Sim → s5io   OUT 0 GPIO=17                                             (Kanal 0 = A 0.0)
…
S5Sim → s5io   COMMIT
s5io → S5Sim   OK                                 bzw.  ERR GPIO 17 ist bereits belegt

S5Sim → s5io   OUT 05                    Ausgangskanäle als Hex-Maske, bei Änderung, mind. alle 100 ms
s5io → S5Sim   IN 81                     Eingangskanäle als Hex-Maske, bei Änderung, mind. alle 100 ms
S5Sim → s5io   BYE
```

- Kanalnummer = Bitnummer innerhalb der Pi-Baugruppe (Byte·8 + Bit).
- Bei `COMMIT` prüft `s5io`, ob alle Pins existieren und nicht doppelt belegt sind.
- Das Format kann sich bei der Umsetzung noch ändern; es bekommt eine Versionsnummer (`1`).

## 4. Umsetzung in Schritten

| Schritt | Inhalt | Prüfung |
|---|---|---|
| 1 | E/A-Schnittstelle im Kern (`IoDevice`), Protokoll-Bibliothek, **simulierter `s5io`** auf dem Mac | Unit-Tests, S5Sim gegen simulierten Pi |
| 2 | `s5io` für den Pi: libgpiod 2, Watchdog, Entprellung, systemd-Dienst, Installationsanleitung | auf deinem Pi: LED und Taster am Steckbrett |
| 3 | Baugruppe „Raspberry-Pi-E/A“ im Baugruppenträger: Einstellungen, Darstellung, Verbindungs-LED, Warnungen | auf dem Mac gegen simulierten und echten Pi |
| 4 | Optokoppler-Platine aufbauen und anschließen ([Bauanleitung](optokoppler-platine.md)) | mit 24-V-Netzteil, Taster, Relais |
| 5 | Treiber „Parallelport-Platine“ für die [alte Platine](alte-parallelport-platine.md) (10 Eingänge über 74HC151, 16 Ausgänge über 4076B, nur 11 GPIOs); später MCP23017 | Platine mit Pegelwandler am Pi |
| 6 (optional) | Pi als eigenständiges AG: CPU läuft auf dem Pi, S5Sim auf dem Mac ist nur noch PG und überträgt das Programm (MC5) | Programm übertragen, Mac trennen, Pi steuert weiter |

Schritt 1 und 3 kann ich vollständig auf dem Mac bauen und testen. Für Schritt 2 liefere ich Quelltext und
Bauanleitung; kompilieren und ausprobieren musst du auf dem Pi, weil ich keinen Zugriff auf echte GPIO-Pins
habe.

## 5. Offene Punkte

- Alte Parallelport-Platine: Schaltplan ausgewertet; vor dem Anschluss Einschalt-Reset nachrüsten,
  Eingangswiderstände für 24 V tauschen, Ausgangsstrom messen (siehe [Auswertung](alte-parallelport-platine.md)).
- Welche 24-V-Komponenten sollen gesteuert werden (Relaisspulen, Lampen, Ventile; Ströme)?
- Netzwerk: feste IP-Adressen oder Namen (`pi4.local`) – beides wird unterstützt.
