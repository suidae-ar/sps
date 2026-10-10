# s5io auf dem Raspberry Pi installieren

`s5io` ist der kleine Dienst auf dem Pi, mit dem S5Sim die GPIO-Pins schaltet und liest. Er braucht
kein Qt und keinen Bildschirm.

> **Stand:** Mit simulierten Ein-/Ausgängen auf dem Mac getestet (Protokoll, Watchdog, Verbindungsabbruch,
> Wiederverbinden). Der GPIO-Teil (libgpiod 2) ist gegen die offizielle Header-Datei von libgpiod 2.2
> geprüft, aber **noch nicht auf einem echten Pi ausprobiert** – bitte beim ersten Test genau hinschauen.

## 1. Voraussetzungen

- Raspberry Pi 4, Pi 5 oder Pi Zero (W / 2 W für Netzwerk) mit aktuellem Raspberry Pi OS
- **libgpiod Version 2.** Prüfen:

  ```sh
  apt policy libgpiod-dev        # Kandidat muss 2.x sein
  ```

  Raspberry Pi OS auf Basis von Debian 13 („Trixie“) liefert libgpiod 2. Auf dem älteren „Bookworm“
  gibt es nur 1.6 – dann läuft `s5io` nur mit `--sim`, bis das System aktualisiert ist.

## 2. Bauen

```sh
sudo apt install git build-essential cmake pkg-config libgpiod-dev
git clone <Adresse deines Repositorys> s5sim
cd s5sim
cmake -S . -B build
cmake --build build -j4 --target s5io s5io_tests
./build/s5io_tests            # Tests des Dienstes (mit Simulation)
```

CMake meldet dabei `s5io: libgpiod 2.x gefunden – GPIO-Betrieb möglich`. Fehlt Qt auf dem Pi, wird die
Oberfläche einfach nicht gebaut – für `s5io` wird sie nicht gebraucht.

## 3. Ausprobieren

```sh
./build/s5io --verbose
```

Die Ausgabe nennt das Modell und den Port, z.B.
`s5io GPIO (Raspberry Pi 4 Model B Rev 1.5) wartet auf S5Sim an 0.0.0.0:2511`.

Dann in S5Sim auf dem Mac:

1. Im Baugruppenträger *Baugruppe → Raspberry-Pi-E/A stecken …*
2. **Rechner:** Name oder IP-Adresse des Pi (z.B. `raspberrypi.local` oder `192.168.1.20`), Port 2511.
3. Adressen und Pinbelegung prüfen – die Standardbelegung entspricht der [Pinbelegung](pinbelegung.md).
4. Die Verbindungs-LED auf der Pi-Baugruppe wird grün; `s5io` meldet „Verbunden mit …“ und
   „Konfiguration: 8 Eingänge, 8 Ausgänge“.

Für einen ersten Test ohne Optokoppler-Platine genügen eine LED mit Vorwiderstand (z.B. 330 Ω) von
GPIO 17 (Pin 11) nach GND und ein Taster von GPIO 5 (Pin 29) nach GND. Für den Taster in den Einstellungen
der Pi-Baugruppe bei E 0.0 **Pull-up** wählen und **invertiert** ankreuzen.

Mit `--sim` lässt sich `s5io` auch ohne angeschlossene Hardware testen; Eingänge werden dann in der
Konsole gesetzt (`e 5 1` = GPIO 5 auf 1, `s` = Zustand).

## 4. Als Dienst einrichten

```sh
sudo cmake --install build                  # installiert /usr/local/bin/s5io
sudo cp s5io/s5io.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now s5io
journalctl -u s5io -f                       # Meldungen verfolgen
```

Der Dienst läuft als eigener, unprivilegierter Benutzer in der Gruppe `gpio` und startet nach einem
Absturz automatisch neu.

## 5. Alles auf einem Pi

Sollen S5Sim und `s5io` auf demselben Pi 4 oder Pi 5 laufen:

```sh
sudo apt install qt6-base-dev
cmake -S . -B build && cmake --build build -j4
```

In der Pi-Baugruppe als Rechner `127.0.0.1` eintragen. Damit `s5io` dann nur lokal erreichbar ist, in der
Dienstdatei `--bind 127.0.0.1` ergänzen.

## Verhalten im Betrieb

| Situation | Was passiert |
|---|---|
| S5Sim sendet 0,5 s lang nichts (hängt, Netzwerk weg) | Watchdog: `s5io` schaltet alle Ausgänge ab |
| Verbindung bricht ab oder S5Sim wird beendet | Ausgänge aus; S5Sim zeigt rote Verbindungs-LED und STÖRUNG an der CPU, die Pi-Eingänge gehen auf 0, alle 2 s neuer Verbindungsversuch |
| zweites S5Sim will sich verbinden | wird abgelehnt („bereits verbunden“) |
| CPU in STOP | S5Sim sendet 0 – Ausgänge aus |
| Konfiguration ungültig (Pin doppelt, nicht vorhanden) | `s5io` lehnt ab, S5Sim zeigt die Meldung |

## Fehlersuche

| Meldung in S5Sim | Ursache |
|---|---|
| „Rechner … nicht gefunden“ | Name falsch oder `.local`-Auflösung im Netz nicht möglich – IP-Adresse eintragen |
| „Verbindung abgelehnt – läuft s5io auf …?“ | Dienst läuft nicht (`systemctl status s5io`) oder falscher Port |
| „s5io meldet: GPIO-Chip … lässt sich nicht öffnen“ | Benutzer nicht in der Gruppe `gpio` |
| „s5io meldet: GPIO-Leitungen … sind belegt“ | Ein anderes Programm benutzt die Pins (z.B. ein zweiter s5io oder ein Python-Skript) |
