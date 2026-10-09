# Bauanleitung: Optokoppler-Platine für 24 V

Eine Lochrasterplatine mit **8 Eingängen** und **8 Ausgängen**, die 24-V-Signale galvanisch getrennt mit
dem Raspberry Pi verbindet. Als Optokoppler dient der **CNY17-3** – derselbe Typ, der auch auf der alten
Parallelport-Platine sitzt.

> **Sicherheit**
> - Nur mit **24 V aus einem Netzteil mit sicherer Trennung** (SELV, z.B. Hutschienen-Netzteil 24 V / 1–2,5 A)
>   betreiben, Zuleitung mit einer Sicherung (z.B. 1 A träge) absichern.
> - Die Masse des Pi (GND) und die Masse der 24-V-Seite (M) **niemals verbinden** – sonst ist die Trennung weg.
> - Not-Aus und Schutzfunktionen fest verdrahten, nicht über Pi oder Simulator.
> - 230 V hinter Relaiskontakten nur durch eine Elektrofachkraft.

## Aufbau der Platine

```
 ┌────────────────────── Feldseite 24 V ───────────────┐ │ ┌──── Pi-Seite 3,3 V ────┐
 │ Klemmen E0…E7, M                                    │ │ │                        │
 │   └─► Eingangsstufe (R, Z-Diode, Schutzdiode) ──► CNY17-3 ──► GPIO + Pull-down   │──► Flachband
 │ Klemmen +24 V, M, A0…A7                             │ │ │                        │    zum Pi
 │   ◄── Relaisstufe (BC337, Relais, Freilaufdiode) ◄── CNY17-3 ◄── GPIO + 470 Ω    │
 └─────────────────────────────────────────────────────┘ │ └────────────────────────┘
                                                Trennfuge: mind. 3 mm Abstand, die
                                                Optokoppler überbrücken sie
```

Die Optokoppler sitzen genau über der Trennfuge: Pins 1–3 (Leuchtdiode) auf der einen, Pins 4–6
(Fototransistor) auf der anderen Seite – bei den Eingängen die LED auf der Feldseite, bei den Ausgängen auf
der Pi-Seite.

### Anschlüsse des CNY17-3 (6-polig DIP, Draufsicht, Kerbe links)

```
        ┌───∪───┐
 Anode 1│●      │6 Basis
Kathode 2│       │5 Kollektor
   n.c. 3│       │4 Emitter
        └───────┘
```

Stromübertragungsverhältnis (CTR) des CNY17-**3**: 100–200 % bei 10 mA Diodenstrom.

## Eingangsstufe (je Eingang)

```
 Feldseite (24 V)                              Pi-Seite (3,3 V)

 Klemme E x.y                                  +3,3 V (Pi Pin 1/17) ── Pin 5 (Kollektor)
     │
 [R1 1,5 kΩ]                                   Pin 4 (Emitter) ──┬── GPIO (z.B. GPIO 5 = Pi Pin 29)
     │                                                           │
 Z1 10 V  (Kathode oben)                                    [R2 10 kΩ]
     │                                                           │
     ├───────────────────┐                                  GND (Pi)
     │                   │
 Pin 1 (Anode)       D1 1N4148                         Pin 6 (Basis) bleibt offen
   CNY17-3           (Kathode oben)
 Pin 2 (Kathode)         │
     │                   │
     ├───────────────────┘
     │
 Klemme M (0 V)
```

| Teil | Wert | Aufgabe |
|---|---|---|
| R1 | 1,5 kΩ, 0,5 W | begrenzt den Diodenstrom |
| Z1 | Z-Diode 10 V, 0,5 W (z.B. BZX55C10 / BZX79-C10), Kathode zur Klemme | Schaltschwelle: unter ca. 11 V sicher „0“ |
| D1 | 1N4148, antiparallel zur Leuchtdiode | Verpolschutz der Leuchtdiode (Sperrspannung nur 6 V) |
| R2 | 10 kΩ | Pull-down: ohne Signal sicher Low |

**Rechnung**

- Schaltschwelle ≈ Z-Spannung + Durchlassspannung der LED ≈ 10 V + 1,2 V ≈ **11,2 V**.
- Bei 24 V: I = (24 − 11,2) V / 1,5 kΩ ≈ **8,5 mA**; Leistung an R1 ≈ 0,11 W.
- Bei 30 V (oberes Ende): I ≈ 12,5 mA; Leistung an R1 ≈ 0,23 W – deshalb 0,5 W.
- Bei 15 V: I ≈ 2,5 mA. Der Fototransistor muss nur 3,3 V / 10 kΩ = 0,33 mA liefern – er schaltet
  sicher durch, der GPIO sieht ca. 3,1 V = **1**.
- Bei Verpolung (−24 V): Strom über Z1 und D1 in Durchlassrichtung, ≈ 15 mA, unkritisch.
- Ergebnis: 0 V … ca. 11 V → **0**, ab ca. 12 V bis 30 V → **1**, wie man es von 24-V-Eingängen kennt.

**Logik:** Spannung an der Klemme → GPIO High. Der Eingang ist also **nicht invertiert**; ein offener oder
abgerissener Draht ergibt „0“.

**Optional**

- Anzeige-LED auf der Feldseite: Low-Current-LED in Reihe zu R1 und Z1 = 8,2 V wählen (die LED verschiebt
  die Schwelle um ca. 1,8 V).
- Störungsreiche Umgebung: 1 MΩ von Pin 6 (Basis) nach Pin 4 (Emitter) und 100 nF parallel zu R2.
  Taster werden zusätzlich in `s5io` entprellt.

## Ausgangsstufe mit Relais (je Ausgang)

```
 Pi-Seite (3,3 V)                         Feldseite (24 V)

 GPIO ──┬──[R3 470 Ω]── Pin 1 (Anode)     Pin 5 (Kollektor) ── +24 V
 (z.B.  │                CNY17-3
 GPIO 17,                                 Pin 4 (Emitter)                 +24 V ──┬────────────┐
 Pin 11)│                                     │                                   │            │
    [R6 47 kΩ]                            [R4 4,7 kΩ]                     Relaisspule K1   D2 1N4007
        │                                     │                                   │      (Kathode oben)
 GND ───┴────────────── Pin 2 (Kathode)       ├────────── Basis T1                ├────────────┘
                                              │           BC337-40  Kollektor ────┘
                                          [R5 10 kΩ]                Emitter
                                              │                        │
                           M (0 V) ───────────┴────────────────────────┘
```

| Teil | Wert | Aufgabe |
|---|---|---|
| R3 | 470 Ω | Diodenstrom aus dem GPIO |
| R6 | 47 kΩ | hält den Ausgang aus, solange der Pin noch nicht konfiguriert ist (Start des Pi) |
| R4 | 4,7 kΩ | Basiswiderstand |
| R5 | 10 kΩ | sperrt T1 sicher, wenn der Optokoppler aus ist |
| T1 | BC337-40 (45 V, 800 mA) | schaltet die Relaisspule |
| D2 | 1N4007 | Freilaufdiode gegen die Abschaltspannung der Spule |
| K1 | Relais mit 24-V-Spule (Spulenstrom ≤ 40 mA) | potenzialfreier Kontakt für die Last |

**Rechnung**

- Diodenstrom: (3,3 − 1,2) V / 470 Ω ≈ **4,5 mA** je Ausgang; 8 Ausgänge ≈ 36 mA – innerhalb der ca.
  50 mA, die alle GPIOs zusammen liefern dürfen.
- Bei 4,5 mA ist das CTR kleiner als im Datenblatt (10 mA); mit vorsichtig angenommenen 50 % liefert der
  Optokoppler ≥ 2,2 mA Basisstrom. Bei einer Stromverstärkung von nur 20 (Sättigung) reicht das für
  ≥ 40 mA Kollektorstrom – typische 24-V-Relaisspulen brauchen 10–25 mA.
- Logik: GPIO High → Relais an. Beim Start des Pi und bei Verbindungsabbruch (Watchdog in `s5io`) ist der
  GPIO Low → Relais **aus**.

### Variante: Transistorausgänge ohne Relais

Für Lasten bis einige 100 mA (Lampen, kleine Ventile) kann statt T1 je Kanal ein Kanal des **ULN2803A**
(8 Darlington-Stufen, 50 V, 500 mA je Kanal, Freilaufdioden eingebaut) verwendet werden: Emitter des
Optokopplers → Eingang des ULN2803A (Vorwiderstand ist eingebaut), Pin COM an +24 V, Last zwischen +24 V und
Ausgang. Achtung: Der ULN2803A schaltet gegen M („low-side“), die S5-Ausgänge schalten dagegen +24 V; und
die Gesamtverlustleistung des Bausteins begrenzt, wie viele Kanäle gleichzeitig hohen Strom führen dürfen.

## Stückliste (8 Eingänge, 8 Ausgänge)

| Anzahl | Teil |
|---|---|
| 16 | Optokoppler CNY17-3 (vorhandene von der alten Platine verwenden, falls sie auslötbar sind) |
| 16 | IC-Fassung 6-polig |
| 8 | Widerstand 1,5 kΩ, 0,5 W (R1) |
| 8 | Z-Diode 10 V, 0,5 W (Z1) |
| 8 | 1N4148 (D1) |
| 8 | Widerstand 10 kΩ (R2) |
| 8 | Widerstand 470 Ω (R3) |
| 8 | Widerstand 4,7 kΩ (R4) |
| 8 | Widerstand 10 kΩ (R5) |
| 8 | Widerstand 47 kΩ (R6) |
| 8 | Transistor BC337-40 (T1) |
| 8 | 1N4007 (D2) |
| 8 | Relais 24 V, z.B. Printrelais 1 Wechsler (K1) |
| 3 | Schraubklemmenleiste RM 5,08: Eingänge E0–E7 + M (9-polig), Ausgänge (Relaiskontakte), Versorgung +24 V / M |
| 1 | Wannenstecker 2 × 20 + Flachbandkabel zum Pi (oder 2 Wannenstecker 2 × 10) |
| 1 | Lochrasterplatine 160 × 100 mm |
| 1 | Sicherungshalter mit Feinsicherung 1 A träge für +24 V |

## Inbetriebnahme Schritt für Schritt

1. **Sichtprüfung:** Keine Verbindung zwischen GND (Pi-Seite) und M (Feldseite) – mit dem Durchgangsprüfer
   kontrollieren.
2. **Eingänge ohne Pi:** Pi-Seite aus einem Labornetzteil mit 3,3 V versorgen. 24 V an E0 → am Emitter
   (Pin 4) ca. 3,1 V; Klemme offen → 0 V. Mit einstellbarer Spannung die Schaltschwelle prüfen (ca. 11–12 V).
3. **Ausgänge ohne Pi:** 3,3 V über den Anschluss eines GPIO anlegen → Relais zieht an; 0 V → fällt ab.
4. **Am Pi ohne S5Sim:** Flachbandkabel bei ausgeschaltetem Pi anschließen. Dann mit den libgpiod-Werkzeugen
   testen: `gpioget GPIO5` liest E 0.0, `gpioset GPIO17=1` schaltet A 0.0 ein (Strg+C beendet).
5. **Mit S5Sim:** sobald `s5io` und die Pi-Baugruppe fertig sind (siehe [Plan](plan.md)).

## Erweiterung

Reichen 8 + 8 nicht, gibt es zwei Wege, ohne mehr Pins am Pi:

- **Multiplex-Eingänge** mit 74HC151: 8 Eingänge über 4 Pins (siehe
  [alte Parallelport-Platine](alte-parallelport-platine.md)).
- **I²C-Portbausteine** MCP23017 an GPIO 2/3: je Baustein 16 E/A, bis zu 8 Bausteine – dahinter wieder die
  hier beschriebenen Ein- und Ausgangsstufen.
