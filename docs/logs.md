# Log format

NDJSON, one event per line, daily rotation (by RTC UTC date; a single
`log_0000-00-00.ndjson` when no RTC is fitted). Files live in `/logs/` on the SD.

Feed records:

```json
{"boot":7,"ms":12345,"t":"2026-06-19T14:33:21Z","src":"tcp:192.168.0.16:10110","raw":"$GPRMC,..."}
```

- `boot` — boot counter persisted in NVS, +1 on every power-up/reset.
- `ms` — `millis()` since that boot; resets on reboot. `(boot, ms)` orders
  every record even with no RTC.
- `t` — RTC UTC, or `""` with no clock (reconstruct from GPS in the stream).

Each file open (boot, remount, date rollover) first writes a marker with no `raw`:

```json
{"boot":7,"ms":812,"t":"","event":"open","reset":"TASK_WDT"}
```

`reset` is `esp_reset_reason()` (`POWERON`, `BROWNOUT`, `TASK_WDT`, `PANIC`, …) —
a post-trip record of every restart. `tools/parse_log.py` skips markers.

See [PLAN.md §Format on disk](../PLAN.md#format-on-disk) for the rationale.
