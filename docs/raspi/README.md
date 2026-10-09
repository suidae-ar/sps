# S5Sim mit dem Raspberry Pi – Übersicht

Ziel: Die Ein- und Ausgänge des Simulators werden mit **echter Hardware** verbunden. Ein Raspberry Pi
liest 24-V-Eingänge über Optokoppler ein und schaltet über Optokoppler und Relais 24-V-Ausgänge –
gesteuert von einem AWL-Programm in S5Sim.

> **Stand:** Planung und Dokumentation. Die Software (`s5io` auf dem Pi, Pi-Baugruppe in S5Sim) ist noch
> nicht umgesetzt.

## Dokumente

| Dokument | Inhalt |
|---|---|
| [plan.md](plan.md) | Aufbau, Protokoll, Sicherheitskonzept, Umsetzungsschritte |
| [pinbelegung.md](pinbelegung.md) | Stiftleiste des Pi, vorgeschlagene Belegung für 8 Eingänge und 8 Ausgänge |
| [optokoppler-platine.md](optokoppler-platine.md) | Bauanleitung: 24-V-Eingänge und -Ausgänge mit CNY17-3 |
| [alte-parallelport-platine.md](alte-parallelport-platine.md) | Auswertung des Schaltplans der alten Parallelport-Platine, Anschluss an den Pi |

## Grundidee in einem Bild

```
 ┌──────────── Mac (oder Pi) ─────────────┐          ┌──────────── Raspberry Pi ───────────┐
 │ S5Sim                                  │  Netzwerk │ s5io (Dienst, ohne Bildschirm)      │
 │  ├─ CPU, Editor, Baugruppenträger      │  TCP     │  ├─ GPIO über libgpiod               │
 │  └─ Baugruppe „Raspberry-Pi-E/A“  ─────┼─────────►│  ├─ Watchdog: Ausgänge aus bei       │
 │       E 0.0 … 0.7 / A 0.0 … 0.7        │◄─────────┼─ │   Verbindungsverlust               │
 └────────────────────────────────────────┘          │  └─ Pins ──► Optokoppler-Platine     │
                                                     └──────────────────┬──────────────────┘
                                                                        │ galvanisch getrennt
                                                                 24-V-Taster, Endschalter,
                                                                 Relais, Lampen, Ventile
```

- **S5Sim auf dem Mac, Pi als E/A-Knoten** (Variante A): Im Baugruppenträger steckt eine Baugruppe
  „Raspberry-Pi-E/A“ mit der Netzwerkadresse des Pi.
- **Alles auf einem Pi:** S5Sim und `s5io` laufen auf demselben Pi, als Adresse wird `127.0.0.1`
  eingetragen. Für die Oberfläche eignen sich Pi 4 und Pi 5; der Pi Zero ist dafür zu schwach, als reiner
  E/A-Knoten aber gut geeignet.

## Vorhandene Geräte

| Gerät | Eignung |
|---|---|
| Raspberry Pi 4 | E/A-Knoten oder komplette Station mit Oberfläche |
| 2 × Raspberry Pi 5 | E/A-Knoten oder komplette Station mit Oberfläche |
| Raspberry Pi Zero | E/A-Knoten (mit WLAN nur Zero W / Zero 2 W; die Stiftleiste muss ggf. eingelötet werden) |

Alle haben dieselbe 40-polige Stiftleiste, die Pinbelegung gilt für alle.

## Sicherheit vorweg

- Die GPIO-Pins vertragen **nur 3,3 V**. 5 V oder 24 V direkt am Pin zerstören den Pi.
- 24-V-Signale nur über **Optokoppler** (galvanische Trennung) anschließen, Lasten nur über Treiber oder Relais.
- **Not-Aus und Schutzfunktionen** gehören fest verdrahtet in die Anlage – nie über Pi oder Simulator.
- Netzspannung (230 V) hinter Relaiskontakten darf nur eine Elektrofachkraft anschließen.
