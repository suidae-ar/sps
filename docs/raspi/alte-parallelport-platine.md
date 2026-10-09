# Die alte Parallelport-Platine am Raspberry Pi

Eine selbst gebaute E/A-Platine von damals, die am Druckerport (Centronics, 25-poliger Sub-D) des PCs
hing. Grundlage dieser Auswertung ist der gescannte Schaltplan
(`unterlagen/Schaltplan_alte-parallelport-platine.jpeg`, nicht im Repository).

## Ergebnis in Kürze

- **10 Eingänge** (8 über den Multiplexer 74HC151, 2 direkt) und **16 Ausgänge** (4 × D-Register 4076B),
  alle über Optokoppler CNY17-3 galvanisch getrennt.
- Die Platine braucht am Pi nur **11 GPIO-Pins** für 26 Ein- und Ausgänge – ideal für die wenigen Pins des Pi.
- Die Eingänge sind gut verwendbar; für 24 V müssen die Vorwiderstände getauscht werden.
- Die Ausgänge können nur wenige mA schalten und haben beim Einschalten einen **zufälligen Zustand** –
  für eine echte Steuerung sind zwei kleine Änderungen nötig.
- Zwischen Pi (3,3 V) und Platine (5 V) gehört ein kleiner **Pegelwandler**.

## Bauteile

| Bezeichnung | Bauteil | Aufgabe |
|---|---|---|
| V20–V35 | 16 × CNY17-3 | Ausgänge 0–15 |
| V36–V45 | 10 × CNY17-3 | Eingänge 0–9 |
| D1–D4 | 4 × 4076B (Aufdruck MC14076BCP, Motorola) | 4-Bit-D-Register, je 4 Ausgänge |
| D5 | 74HC151 | 8-zu-1-Multiplexer für die Eingänge 0–7 |
| V1 | 7805 | Spannungsregler 5 V |
| V4–V19 | 16 LEDs mit R1–R16 (1,8 kΩ) | Zustandsanzeige der Ausgänge |
| V3 | LED mit R44 (1,8 kΩ) | Betriebsanzeige |
| S0–S9 | Schalter | Eingänge von Hand setzen (Test) |
| S10 | Schalter | Ein/Aus |
| X1 | 25-poliger Sub-D-Stecker | zum PC (Centronics) |
| X2 | Buchse | Versorgung 8 … 20 V (Steckernetzteil ≥ 300 mA oder 9-V-Block) |
| X3 | Steckbrücke | legt BUSY fest auf High (über R43) oder GND |

Der Aufdruck „MC140776BCP“ ist damit geklärt: Es ist der **MC14076BCP** (4076B).

## Belegung des Sub-D-Steckers X1

| Pin | Signal | Richtung | Verwendung auf der Platine |
|---|---|---|---|
| 2 | D0 | PC → Platine | Daten Bit 0 der Register **und** Adresse S0 des Multiplexers |
| 3 | D1 | PC → Platine | Daten Bit 1 **und** Adresse S1 |
| 4 | D2 | PC → Platine | Daten Bit 2 **und** Adresse S2 |
| 5 | D3 | PC → Platine | Daten Bit 3 |
| 6 | D4 | PC → Platine | Takt Register D1 (Ausgänge 0–3) |
| 7 | D5 | PC → Platine | Takt Register D2 (Ausgänge 4–7) |
| 8 | D6 | PC → Platine | Takt Register D3 (Ausgänge 8–11) |
| 9 | D7 | PC → Platine | Takt Register D4 (Ausgänge 12–15) |
| 12 | PE | Platine → PC | Ausgang Y des Multiplexers (Eingänge 0–7) |
| 13 | SLCT | Platine → PC | Eingang 8 (Pull-up R34 2,2 kΩ) |
| 10 | ACK | Platine → PC | Eingang 9 (Pull-up R33 2,2 kΩ) |
| 11 | BUSY | Platine → PC | fest über Steckbrücke X3 |
| 18–25 | GND | | Masse der Logik |
| 1, 14–17 | – | | nicht belegt |

## So arbeitet die Platine

### Eingänge

- **Feldseite:** Klemme → 1 kΩ (R61–R70) → Leuchtdiode des CNY17-3 → gemeinsame Masse „ext. GND“.
- **Logikseite:** Der Fototransistor zieht die Leitung gegen GND; ohne Signal hält ein Pull-up sie auf
  +5 V (R35–R42 10 kΩ bei den Multiplexer-Eingängen, R33/R34 2,2 kΩ bei ACK/SLCT). Die Schalter S0–S9 liegen
  parallel zum Fototransistor.
- **Die Logik ist invertiert:** Signal an der Klemme → Leitung **Low**.
- **Lesen der Eingänge 0–7:** Adresse 0 … 7 auf D0–D2 anlegen, kurz warten, PE lesen; das ergibt den
  invertierten Zustand des gewählten Eingangs. Der Multiplexer ist dauernd freigegeben (E an GND).
- **Eingänge 8 und 9:** direkt an SLCT und ACK, ebenfalls invertiert.

### Ausgänge

- Je Register: Dateneingänge J0–J3 an D0–D3, Takt T an D4 (D1), D5 (D2), D6 (D3) bzw. D7 (D4).
  Ausgangsfreigabe, Eingangsfreigabe und Rücksetzen (R) liegen fest an GND – die Register sind immer aktiv.
- **Schreiben einer Vierergruppe:** Datenbits auf D0–D3 legen, dann den Takt der Gruppe von Low auf High
  und wieder auf Low schalten – mit der steigenden Flanke übernimmt das Register die Daten.
- Jeder Registerausgang Q treibt über 1 kΩ (R45–R60) die Leuchtdiode eines CNY17-3 und über 1,8 kΩ eine
  Anzeige-LED. **Q = High → Ausgang aktiv.**
- **Feldseite:** Der Fototransistor schaltet die Klemme 0 … 15 gegen die gemeinsame Masse „GND ext.“ –
  16 Open-Collector-Ausgänge.

Da D0–D2 zugleich Daten und Multiplexer-Adresse sind, darf beim Lesen der Eingänge kein Takt anliegen –
die Taktleitungen D4–D7 bleiben dabei Low, dann ändern sich die Ausgänge nicht.

### Versorgung

8 … 20 V an X2 → Verpolschutzdiode 1N4001 (V2) → Schalter S10 → 7805 → 5 V für die Logik. Die Z-Diode
6,2 V (V46) schützt die 5-V-Seite. Die Masse der Logik ist über X1 mit dem PC (künftig dem Pi) verbunden;
die Feldseiten der Ein- und Ausgänge sind durch die Optokoppler davon getrennt.

## Bewertung für den Betrieb mit 24 V

### Eingänge – gut verwendbar, Vorwiderstände tauschen

Mit 1 kΩ fließen bei 24 V etwa (24 − 1,2) V / 1 kΩ ≈ 23 mA, der Widerstand setzt dabei über 0,5 W um –
zu viel für übliche Widerstände. Die Eingänge waren vermutlich für kleinere Spannungen gedacht.

- **Einfach:** R61–R70 gegen **2,2 kΩ, 0,5 W** tauschen (≈ 10 mA, ≈ 0,24 W). Die Schaltschwelle liegt dann
  aber schon bei wenigen Volt.
- **Wie bei der S5:** zusätzlich eine Z-Diode 10 V in Reihe und 1,5 kΩ, 0,5 W – siehe
  [Bauanleitung, Eingangsstufe](optokoppler-platine.md#eingangsstufe-je-eingang). Dann gilt unter ca. 11 V
  sicher „0“, ab ca. 12 V „1“.
- Bei beiden Varianten eine 1N4148 antiparallel zur Leuchtdiode als Verpolschutz ergänzen.

### Ausgänge – zwei Änderungen nötig

1. **Zufälliger Zustand beim Einschalten:** Die 4076B haben keinen Einschalt-Reset (R fest an GND). Bis
   zum ersten Schreiben können beliebige Ausgänge aktiv sein. Abhilfe ohne weiteren GPIO-Pin:
   Pin 15 (R) der vier Register von GND trennen und mit einem **RC-Glied** beschalten – 100 nF von +5 V nach
   Pin 15, 100 kΩ von Pin 15 nach GND. Beim Einschalten entsteht ein kurzer High-Impuls (ca. 10 ms), alle
   Ausgänge starten mit 0.
2. **Kleiner Ausgangsstrom:** Bausteine der Reihe 4000 liefern bei 5 V nur wenige mA. Damit fließen in den
   Optokoppler-Dioden nur etwa 1–3 mA, und die Fototransistoren können entsprechend nur wenige mA schalten.
   Das reicht für Anzeigen und als Steuersignal, nicht für Relaisspulen. Für Relais eine Treiberstufe
   nachschalten (z.B. Transistorstufe mit eigenem Pull-up an 24 V) oder für die Ausgänge die
   [neue Ausgangsstufe](optokoppler-platine.md#ausgangsstufe-mit-relais-je-ausgang) aufbauen.
   **Bitte den tatsächlich schaltbaren Strom einmal nachmessen** – das Datenblatt des 4076B nennt nur
   Mindestwerte.

Die Fototransistoren selbst vertragen 24 V problemlos (CNY17: Kollektor-Emitter-Spannung bis 70 V).

## Anschluss an den Raspberry Pi

### Pegel: 3,3 V am Pi, 5 V auf der Platine

Die Platine sollte bei **5 V** bleiben – mit 3,3 V würden die 4076B die Leuchtdioden und Optokoppler zu
schwach ansteuern. Dazwischen gehört ein kleiner Pegelwandler:

| Richtung | Leitungen | Baustein | Grund |
|---|---|---|---|
| Pi → Platine | D0–D7 (8) | **74HCT245** an 5 V, Richtung fest A → B | Die 4076B erkennen High erst ab 3,5 V; HCT-Eingänge schon ab 2,0 V |
| Platine → Pi | PE, SLCT, ACK (3) | **74LVC245** an 3,3 V (5-V-feste Eingänge) oder je ein Spannungsteiler 10 kΩ / 15 kΩ | Der Pi verträgt keine 5 V |

Die Masse des Pi wird mit der Logikmasse der Platine (X1 Pin 18–25) verbunden. Das ist unbedenklich: Die
24-V-Feldseiten bleiben über die Optokoppler getrennt.

### Belegung Pi ↔ Platine

Gleiche Ausgangspins wie in der [Pinbelegung](pinbelegung.md) vorgeschlagen:

| Pi GPIO | Pi Pin | über | X1 Pin | Signal |
|---|---|---|---|---|
| GPIO 17 | 11 | 74HCT245 | 2 | D0 |
| GPIO 18 | 12 | 74HCT245 | 3 | D1 |
| GPIO 27 | 13 | 74HCT245 | 4 | D2 |
| GPIO 22 | 15 | 74HCT245 | 5 | D3 |
| GPIO 23 | 16 | 74HCT245 | 6 | D4 (Takt Ausgänge 0–3) |
| GPIO 24 | 18 | 74HCT245 | 7 | D5 (Takt Ausgänge 4–7) |
| GPIO 25 | 22 | 74HCT245 | 8 | D6 (Takt Ausgänge 8–11) |
| GPIO 26 | 37 | 74HCT245 | 9 | D7 (Takt Ausgänge 12–15) |
| GPIO 5 | 29 | 74LVC245 / Teiler | 12 | PE (Eingänge 0–7) |
| GPIO 6 | 31 | 74LVC245 / Teiler | 13 | SLCT (Eingang 8) |
| GPIO 12 | 32 | 74LVC245 / Teiler | 10 | ACK (Eingang 9) |
| GND | 6, 9, 14 … | | 18–25 | Masse |

Die Steckbrücke X3 kann bleiben, wie sie ist – BUSY wird nicht ausgewertet.

### In S5Sim

`s5io` erhält einen Treiber „Parallelport-Platine“. Die Platine erscheint in S5Sim als Pi-Baugruppe mit
**EB 0** (Eingänge 0–7), **E 1.0 / E 1.1** (Eingänge 8, 9) und **AB 0 / AB 1** (Ausgänge 0–15); die übrigen
Bits von EB 1 sind „nicht belegt“. Ablauf je Zyklus:

1. **Eingänge lesen:** Taktleitungen D4–D7 auf Low; für Adresse 0 … 7: D0–D2 setzen, ca. 10 µs warten
   (Laufzeit von Multiplexer und Pegelwandler), PE lesen und invertieren. SLCT und ACK lesen und invertieren.
2. **Ausgänge schreiben** (nur geänderte Gruppen): D0–D3 setzen, Takt der Gruppe High, dann Low.
3. **Watchdog:** Bei Verbindungsverlust schreibt `s5io` in alle vier Register 0.

Ein vollständiger Durchlauf dauert unter 1 ms – deutlich kürzer als der 10-ms-Zyklus von S5Sim.

## Nächste Schritte

1. Bauteilwerte auf der Platine prüfen (stimmen sie mit dem Schaltplan überein?).
2. Ausgangsstrom eines Ausgangs messen (Klemme über Amperemeter und z.B. 1 kΩ an 24 V).
3. Einschalt-Reset (RC-Glied) nachrüsten, Eingangs-Vorwiderstände für 24 V tauschen.
4. Pegelwandler-Platinchen bauen (74HCT245 + 74LVC245 oder Spannungsteiler, Sub-D-Buchse, Stecker für den Pi).
5. Treiber in `s5io` – zusammen mit Schritt 5 des [Plans](plan.md).
