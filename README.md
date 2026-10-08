# S5Sim – Simulator für SIMATIC-S5-Programme (AWL)

Desktop-Simulator für STEP-5-Programme in Anweisungsliste, gebaut mit C++20 und Qt 6.

## Bauen und starten

```sh
brew install qt cmake          # einmalig
cmake -S . -B build
cmake --build build -j8
./build/s5core_tests           # Tests von Parser und CPU
./build/s5mc5_tests            # Tests des MC5-Übersetzers
open build/S5Sim.app
```

## Aufbau

| Verzeichnis | Inhalt |
|---|---|
| `core/` | Kern ohne Qt: `parser` (AWL → Bausteine), `cpu` (Zyklus, VKE, AKKU, Zeiten, Zähler, DBs), `mc5` (Assembler/Disassembler MC5) |
| `gui/` | Qt-Oberfläche: Editor mit Status-Spalte, E/A-Panel, Variablentabelle |
| `examples/` | Beispielprogramme (auch im Menü *Datei → Beispiele*) |
| `tests/` | Tests für Parser und CPU |

## Was die CPU kann

- Bausteine OB 1 (zyklisch), OB 21 (Neuanlauf), PB, FB, SB, DB
- FB-Parameter: `NAME`, `BEZ` mit den Arten E/A (BI, BY, W), D (KM KH KY KC KF KT KZ), B, T, Z;
  Formaloperanden `=EIN`, Versorgung beim Aufruf (`EIN :E 0.0`), Durchreichen an weitere FBs, Typprüfung beim Übersetzen
- Verknüpfungen `U UN O ON O U( O( ) = S R`, „UND vor ODER“, Erstabfrage/VKE-Begrenzung
- `L LC T` mit EB/EW/AB/AW/MB/MW/DW/DL/DR, Konstanten KF KH KM KY KB KT KZ KC
- Zeiten `SI SV SE SS SA R` mit Zeitbasis 10 ms … 10 s, Zähler `ZV ZR S R`
- Festpunktarithmetik `+F -F XF :F`, Vergleiche `!=F ><F >F >=F <F <=F`
- `UW OW XOW KEW KZW SLW SRW I D TAK`, Sprünge `SPA SPB SPZ SPN SPP SPM SPO`, `BE BEA BEB`, `A DB`, `STP`
- Prozessabbild: Eingänge werden am Zyklusanfang gelesen, Ausgänge am Zyklusende ausgegeben, im STOP sind sie 0
- Laufzeitfehler (fehlender DB, Endlosschleife, Schachtelungstiefe …) führen zu STOP mit Fehlerzeile

## MC5-Maschinencode

- Assembler AWL → MC5 und Disassembler MC5 → AWL, Zielsystem wählbar: S5-90U/95U, 100U, 115U, 135U/155U
- Bausteinkopf (7070, Typ, Nummer, Länge), FB-Parameterblock (NAME/BEZ), FB-Aufrufe mit Parameterliste,
  Formaloperanden, relative Sprünge, Netzwerke (`***` = BLD 255)
- Export/Import als Speicherabbild (`*.mc5`: Bausteine mit Kopf hintereinander), Ansicht „Maschinencode“
- Prüfung je Zielsystem: `XF`, `:F`, Datenbits `U D n.m`, `P/SU/RU` mit E/A/M nur bei 135U/155U;
  ergänzende Operationen wie bei STEP 5 nur im FB; Parameternamen max. 4 Zeichen
- Codes nach den Siemens-Operationslisten (Auflistung des Maschinencodes); getestet sind alle Einzelcodes
  und der Rundlauf AWL → MC5 → AWL → MC5 (bitgleich)

Nicht belegt und deshalb abgelehnt statt geraten: Parameterart **Z** im FB-Kopf und das Durchreichen von
Formaloperanden an einen weiteren FB. Abgeleitet (nicht direkt dokumentiert): Codierung von T/Z-Aktualoperanden
beim FB-Aufruf. PG-Kennung und Bibliotheksnummer im Kopf werden mit 0 belegt. S5D-Dateien werden noch nicht
gelesen (Format nur teilweise bekannt).

## Noch nicht enthalten

Übertragung zur echten CPU (AS511), S5D-Dateien, `FR`, `LW =`, indirekte Adressierung (`B MW`), Peripherie (`L PB/PW`),
Gleitpunkt, Alarme/Weckalarm-OBs, KOP/FUP-Darstellung, Haltepunkte.

## Entwickleroptionen

```sh
S5Sim --example 5 --inputs 0.0 --run --screenshot bild.png --delay 2000
```
