# Post-trip tools

Python utilities for processing the captured logs after a deployment.
See [../PLAN.md](../PLAN.md) Phase 6.

## `parse_log.py` — replay a capture into InstrumentSamples ✅

Feeds a captured log through SailinGrace's own `NMEA0183Parser` +
`NMEACoalescer`, so the offline replay produces the same `InstrumentSample`
rows the live ingest would. Reads both the ESP32 logger's **NDJSON** and the
raw **timestamped-log** (discovery) format; emits CSV.

```bash
# Run with a Python that has pynmea2 — e.g. SailinGrace's venv:
~/SailinGrace/.venv/bin/python parse_log.py \
    ../data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log \
    --sailingrace ~/SailinGrace --magvar 14 -o samples.csv
```

- `--sailingrace PATH` (or `$SAILINGRACE_REPO`) points at the SailinGrace
  repo for the parser + `pynmea2`. Default `../SailinGrace`.
- `--magvar` sets magnetic variation for M→T conversion (Lynx ≈ 14 °W).

Verified against the real Lynx dockside capture: 4493 lines → 116 samples at
~1 Hz, position/heading/wind matching the connection field notes.

## `inject_to_sailingrace.py` — POST a capture to a running backend (planned)

NDJSON → `POST /observations` on a running SailinGrace backend so the routing
UI can replay the trip. Not yet implemented.
