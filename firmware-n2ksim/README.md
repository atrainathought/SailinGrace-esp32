# N2K bench sensor simulator

ESP32 firmware that puts **known, scripted sensor data on an NMEA 2000
backbone**, for bench-testing the Pi + PiCAN-M relay, a laptop N2K adapter
and SailinGrace end to end. Separate from the WiFi logger in
[`../firmware`](../firmware) — same board, different firmware.

> **Bench only.** It transmits fake heading and wind. Never connect it to a
> boat bus with live instruments or an autopilot.

Full procedure, parts list and the expected value at every stage:
[SailinGrace-pi `docs/bench_test.md`](https://github.com/atrainathought/SailinGrace-pi/blob/master/docs/bench_test.md)
and [`docs/bench_materials.md`](https://github.com/atrainathought/SailinGrace-pi/blob/master/docs/bench_materials.md).

## What it sends (120 s cycle)

| PGN | Data | Rate | Profile |
|---|---|---|---|
| 130306 | Apparent wind | 10 Hz | AWS 10 kn (0–60 s) → 20 kn; AWA +30° (0–40) → +45° (40–80) → 30° port (80–120) |
| 127250 | Heading, magnetic | 10 Hz | 350° → 10° ramp, wraps through 0° at t = 60 s |
| 129026 | COG/SOG rapid | 10 Hz | COG = heading, SOG 7.0 kn |
| 129025 | Position rapid | 10 Hz | from 41.4500 N 71.3200 W, dead-reckoned along COG |
| 128259 | Speed through water | 1 Hz | 6.5 kn |
| 128267 | Water depth | 1 Hz | 25.0 m |

Plus address claim (60928) and product info (126996, fast-packet) on request,
handled by the library. Constants live in [`src/config.h`](src/config.h).

## Wiring (Feather HUZZAH32)

ESP32 built-in CAN (TWAI) → 3.3 V transceiver (Adafruit CAN Pal /
SN65HVD230) → M12 drop. Pins avoid the SD card (33, 5/18/19) and LED (13).

| ESP32 | Transceiver | N2K M12 |
|---|---|---|
| GPIO 32 | TX / CTX / D | — |
| GPIO 27 | RX / CRX / R | — |
| 3V3 | VCC | — |
| GND | GND | pin 3 (black) |
| — | CANH | pin 4 (white) |
| — | CANL | pin 5 (blue) |

No 120 Ω termination on the transceiver board — it's a drop, not a bus end.

## Build / flash

```bash
cd firmware-n2ksim
pio run -t upload          # featheresp32 (default); -e esp32dev for a DevKit
pio device monitor         # 115200 — one line/s of "script truth"
```

Serial output, once a second:

```
t= 42.0 src=42 aws=10kn awa=45 hdg=357.0 bsp=6.5 sog=7.0 depth=25.0 lat=41.45010 lon=-71.32000
```

`src` is the claimed N2K source address (42 unless another node already has
it). `WARN: SendMsg failed` means nothing else on the bus is ACKing — plug in
the Pi or the laptop adapter.

## Versions

Pinned to `espressif32@6.9.0` (Arduino-ESP32 core 2.0.17),
`NMEA2000-library@4.25.1`, `NMEA2000_esp32@1.0.3`. `NMEA2000_esp32` does not
compile on Arduino core 3.x. Moving to an ESP32-S3 or core 3.x means switching
the CAN backend to a TWAI-driver one (e.g. `NMEA2000_esp32xx`).

Status: **build-verified** for `featheresp32` and `esp32dev`. Flashed and run
on an ESP32 DevKit V1 (`-e esp32dev`, 2026-11-08) with no bus attached: profile
output correct, `WARN: SendMsg failed` as expected. Not yet run on a backbone.
