# Pinbelegung des Raspberry Pi

Gilt für Raspberry Pi 4, Pi 5 und Pi Zero (alle mit derselben 40-poligen Stiftleiste). Angegeben sind
die **physischen Pins** (1 … 40, Pin 1 ist der mit dem eckigen Lötpad, nahe der Ecke der Platine) und die
**GPIO-Nummern** (BCM-Zählung), wie sie Linux und `s5io` verwenden.

> Alle GPIO-Pins arbeiten mit **3,3 V** und sind **nicht 5-V-fest**. Belastung je Pin höchstens etwa
> 16 mA, alle Pins zusammen nicht mehr als etwa 50 mA.

## Vorschlag: 8 Eingänge und 8 Ausgänge

Die Belegung lässt die Pins mit Sonderfunktionen frei: I²C (GPIO 2/3, für spätere Erweiterungen mit
MCP23017), UART (GPIO 14/15, serielle Konsole) und SPI (GPIO 7–11).

### Eingänge (EB 0)

| S5Sim | GPIO | Pin |
|---|---|---|
| E 0.0 | GPIO 5 | 29 |
| E 0.1 | GPIO 6 | 31 |
| E 0.2 | GPIO 12 | 32 |
| E 0.3 | GPIO 13 | 33 |
| E 0.4 | GPIO 16 | 36 |
| E 0.5 | GPIO 19 | 35 |
| E 0.6 | GPIO 20 | 38 |
| E 0.7 | GPIO 21 | 40 |

### Ausgänge (AB 0)

| S5Sim | GPIO | Pin |
|---|---|---|
| A 0.0 | GPIO 17 | 11 |
| A 0.1 | GPIO 18 | 12 |
| A 0.2 | GPIO 27 | 13 |
| A 0.3 | GPIO 22 | 15 |
| A 0.4 | GPIO 23 | 16 |
| A 0.5 | GPIO 24 | 18 |
| A 0.6 | GPIO 25 | 22 |
| A 0.7 | GPIO 26 | 37 |

Die Ausgänge liegen auf der Stiftleiste weitgehend nebeneinander (Pins 11–18), die Eingänge am unteren
Ende (Pins 29–40) – das erleichtert die Verdrahtung mit zwei Flachbandkabeln.

### Versorgung und Masse

| Zweck | Pin |
|---|---|
| +3,3 V für die Pi-Seite der Optokoppler | 1 oder 17 |
| GND (Masse des Pi) | 6, 9, 14, 20, 25, 30, 34, 39 |
| +5 V (nur zum Messen, nicht für die Platine verwenden) | 2, 4 |

Frei bleibt GPIO 4 (Pin 7) als Reserve.

## Gesamte Stiftleiste

`E`/`A` = Vorschlag oben, `res.` = reserviert für Sonderfunktion.

| Funktion | GPIO | Pin | Pin | GPIO | Funktion |
|---:|---:|:---:|:---:|:---|:---|
| 3,3 V | | **1** | **2** | | 5 V |
| res. I²C SDA | 2 | **3** | **4** | | 5 V |
| res. I²C SCL | 3 | **5** | **6** | | GND |
| Reserve | 4 | **7** | **8** | 14 | res. UART TX |
| GND | | **9** | **10** | 15 | res. UART RX |
| **A 0.0** | 17 | **11** | **12** | 18 | **A 0.1** |
| **A 0.2** | 27 | **13** | **14** | | GND |
| **A 0.3** | 22 | **15** | **16** | 23 | **A 0.4** |
| 3,3 V | | **17** | **18** | 24 | **A 0.5** |
| res. SPI MOSI | 10 | **19** | **20** | | GND |
| res. SPI MISO | 9 | **21** | **22** | 25 | **A 0.6** |
| res. SPI SCLK | 11 | **23** | **24** | 8 | res. SPI CE0 |
| GND | | **25** | **26** | 7 | res. SPI CE1 |
| res. ID-EEPROM | 0 | **27** | **28** | 1 | res. ID-EEPROM |
| **E 0.0** | 5 | **29** | **30** | | GND |
| **E 0.1** | 6 | **31** | **32** | 12 | **E 0.2** |
| **E 0.3** | 13 | **33** | **34** | | GND |
| **E 0.5** | 19 | **35** | **36** | 16 | **E 0.4** |
| **A 0.7** | 26 | **37** | **38** | 20 | **E 0.6** |
| GND | | **39** | **40** | 21 | **E 0.7** |

## Verhalten beim Einschalten

- Nach dem Einschalten und bis `s5io` läuft, sind alle GPIO-Pins **Eingänge**. Beim Pi 4 haben GPIO 0–8
  einen internen Pull-up, ab GPIO 9 einen Pull-down. Die vorgeschlagenen Ausgänge (GPIO 17–27) liegen damit
  auf Low – die Ausgangsstufen der Optokoppler-Platine sind **aktiv High** und bleiben deshalb aus.
- Beim **Pi 5** (GPIO am RP1-Chip) ist das Verhalten vor dem Start von `s5io` noch nachzumessen. Die
  Ausgangsstufe der [Optokoppler-Platine](optokoppler-platine.md) hat deshalb zusätzlich einen
  Pull-down-Widerstand, der einen undefinierten Pin sicher auf Low hält.
- Fertige Relaismodule, die **aktiv Low** schalten, sind ungeeignet: Sie würden beim Start des Pi kurz oder
  dauerhaft einschalten.

## Hinweise zu den Modellen

- **Pi 5:** Die GPIOs hängen am RP1-Chip. Dessen Gerätenummer (`gpiochip0` oder `gpiochip4`) hängt von der
  Kernelversion ab; `s5io` sucht die Pins deshalb über ihre Namen. Prüfen mit `gpioinfo | grep GPIO17`.
- **Pi Zero:** gleiche Belegung; beim Zero ohne Stiftleiste muss sie eingelötet werden. Netzwerk nur beim
  Zero W und Zero 2 W (WLAN).
- **Versorgung:** Den Pi über sein eigenes Netzteil versorgen (Pi 5: 5 V / 5 A). Soll er aus den 24 V der
  Anlage versorgt werden, einen geregelten DC/DC-Wandler 24 V → 5 V mit ausreichendem Strom verwenden – die
  Masse des Pi wird damit aber mit der 24-V-Masse verbunden, die galvanische Trennung der Optokoppler ist
  dann für die Versorgung aufgehoben.

## Nützliche Befehle auf dem Pi

```sh
gpiodetect                  # GPIO-Chips anzeigen
gpioinfo | grep -E "GPIO(5|17)\b"   # Pinnamen und Zustand
gpioget --version           # Version von libgpiod (s5io benötigt Version 2)
pinout                      # Belegung der Stiftleiste im Terminal (Raspberry Pi OS)
```
