# SailinGrace — ESP32 (boat-side WiFi data logger)

Tiny, battery-powered ESP32 datalogger that joins the boat's WiFi
instrument network and captures everything it hears (NMEA 0183 over
UDP/TCP or SignalK over WebSocket) to an SD card. Built for the
Newport-Bermuda race aboard *Lynx* but boat-agnostic.

Companion to [`SailinGrace`](https://github.com/atrainathought/SailinGrace)
(the routing application, runs on a laptop) and
[`SailinGrace-pi`](https://github.com/atrainathought/SailinGrace-pi) (the
Pi-based relay for boats with N2K backbone access). This repo is the
*minimum-hardware* option: when all you need is a passive log of what
the boat WiFi puts out for the duration of the race.

**Status:** planning / scaffold only. No working firmware yet — see
[`PLAN.md`](PLAN.md) for the build plan and what needs to happen first
(signal discovery on the boat to find out whether Lynx broadcasts NMEA
0183 or SignalK, which determines the firmware path).

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

## Deploy (once firmware exists)

```bash
# 1. Edit firmware/src/secrets.h with the boat WiFi password
# 2. Flash:
cd firmware && pio run -t upload
# 3. Insert SD, plug in battery, drop in waterproof box
```

See [`docs/deployment.md`](docs/deployment.md) (once written) for the full
boat-day procedure.
