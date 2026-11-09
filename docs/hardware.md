# Hardware

**Last Updated**: 2026-11-09

Parts rationale, SD-card and power choices: [PLAN.md §Hardware](../PLAN.md#hardware)
and [§Power budget](../PLAN.md#power-budget-logging-only). This file is what is
actually built and how it is wired.

## Supported boards

Each board is a PlatformIO env in [`firmware/platformio.ini`](../firmware/platformio.ini);
`default_envs` is the DevKit.

| env | Board | SD | `SD_CS` | RTC | Status LED |
|---|---|---|---|---|---|
| `esp32doit-devkit-v1` (default) | ELEGOO / DOIT ESP32 DevKit V1 (WROOM-32, CP2102, micro-USB) | wired microSD SPI module | 5 | none | GPIO 2 |
| `featheresp32` | Adafruit Feather HUZZAH32 + Adalogger FeatherWing | Adalogger | 33 | PCF8523 | GPIO 13 |
| `adafruit_feather_esp32s3` | Adafruit ESP32-S3 Feather + Adalogger FeatherWing | Adalogger | 10 | PCF8523 | GPIO 13 |

The Adalogger's SD chip-select sits on the Feather "D10" position, which is a
different GPIO per board (33 on the HUZZAH32, 10 on the S3) — hence the
per-env `-DSD_CS`.

## DevKit V1 + microSD module wiring (VSPI)

| microSD module | DevKit |
|---|---|
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| CS | GPIO 5 |
| VCC | 5V for modules with an onboard regulator, 3V3 for bare 3.3 V breakouts |
| GND | GND |

## No RTC (DevKit)

With no RTC every record's `"t"` is `""` and there is one log file,
`/logs/log_0000-00-00.ndjson`, instead of daily rotation. Absolute time is
reconstructed ashore from the GPS sentences in the stream; `(boot, ms)` orders
records across reboots — see [logs.md](logs.md).

## Card

High-endurance / industrial microSD, FAT32. Endurance, not capacity, is the
spec that matters — see [PLAN.md §SD card hardware](../PLAN.md#sd-card-hardware).

## Bench-only reuse: N2K simulator

The same ESP32 can run [`firmware-n2ksim/`](../firmware-n2ksim/) instead, with a
3.3 V CAN transceiver on GPIO 32 (TX) / 27 (RX). Its wiring and safety notes are
in that project's README. Never on a live boat bus.
