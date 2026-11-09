# SailinGrace — ESP32 (boat-side WiFi data logger)

Tiny, battery-powered ESP32 datalogger that joins the boat's WiFi
instrument network, reads the MFD's NMEA 0183-over-TCP feed, and appends
it as NDJSON to a microSD card. Built for the
Newport-Bermuda race aboard *Lynx* but boat-agnostic.

Companion to [`SailinGrace`](https://github.com/atrainathought/SailinGrace)
(the routing application, runs on a laptop) and
[`SailinGrace-pi`](https://github.com/atrainathought/SailinGrace-pi) (the
Pi-based relay for boats with N2K backbone access). This repo is the
*minimum-hardware* option: when all you need is a passive log of what
the boat WiFi puts out for the duration of the race.

**Status:** ✅ working, hardware-verified TCP logger (`firmware/`). Phase 0
discovery on *Lynx* (2026-05-31) locked the path to NMEA 0183 over TCP — see
[`docs/phase0.md`](docs/phase0.md). [`PLAN.md`](PLAN.md) is the design document.
No automated test suite yet; verification is a bench replay of the real *Lynx*
capture ([`docs/flash.md`](docs/flash.md#bench-build-no-boat)).

**Bench tool:** [`firmware-n2ksim/`](firmware-n2ksim/) is a separate
firmware that turns the same ESP32 into an NMEA 2000 *sensor simulator* for
bench-testing the Pi relay (see SailinGrace-pi `docs/bench_test.md`). Bench
only — never on a live boat bus.

## Why this exists

- **Pi 5/4 is overkill** for boats where we only want a passive log.
  Pi Zero 2 W comes down to ~0.5 W, but ESP32 hits ~0.2 W and the whole
  rig fits in an Altoids tin with a single USB power bank.
- **Pi 5/4 is mandatory** for boats where we want the full SailinGrace
  loop (live routing, UI, sensor fusion). See `SailinGrace-pi`.
- **ESP32 fills the middle**: race-only passive logger, no compute, no
  UI, no signalk-server. Drop in, sail, pull the SD afterwards, replay
  through the SailinGrace pipeline ashore.

## What this is NOT

- Not a real-time relay (use `SailinGrace-pi` for that)
- Not running signalk-server (too little RAM)
- Not running SailinGrace routing (the chip can't and shouldn't —
  see [`project-pi-relay-only`](https://github.com/atrainathought/SailinGrace-pi/blob/main/docs/install_rpi.md) for the routing-on-cheap-boat-hardware anti-pattern)

## Deploy

```bash
cp firmware/src/secrets_template.h firmware/src/secrets.h   # fill in the boat WiFi passphrase
cd firmware && pio run -t upload                            # default board: ESP32 DevKit V1
```

- Boards and wiring: [`docs/hardware.md`](docs/hardware.md)
- Flashing and bench mode: [`docs/flash.md`](docs/flash.md)
- Boat-day checklist and LED codes: [`docs/deployment.md`](docs/deployment.md)
- Log format: [`docs/logs.md`](docs/logs.md); post-trip replay: [`tools/`](tools/)
