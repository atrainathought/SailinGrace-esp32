# Log format

**Last Updated**: 2026-11-09

NDJSON, one event per line, daily rotation (by RTC UTC date; a single
`log_0000-00-00.ndjson` when no RTC is fitted). Files live in `/logs/` on the SD.

Feed records:

```json
{"boot":7,"ms":12345,"t":"2026-06-19T14:33:21Z","src":"tcp:192.168.0.16:10110","raw":"$GPRMC,..."}
```

- `boot` — boot counter persisted in NVS, +1 on every power-up/reset. `0` means
  NVS was unavailable on that boot (serial shows a `WARN`); such records can't be
  ordered across reboots.
- `ms` — `millis()` since that boot; resets on reboot. `(boot, ms)` orders
  every record even with no RTC.
- `t` — RTC UTC, or `""` with no clock (reconstruct from GPS in the stream).

Each file open first writes a marker with no `raw`:

```json
{"boot":7,"ms":812,"t":"","event":"open","why":"boot","reset":"TASK_WDT"}
```

- `why` — `boot`, `remount` (after an SD error, or a card inserted after boot) or
  `rotate` (UTC date rollover).
- `reset` — `esp_reset_reason()` (`POWERON`, `BROWNOUT`, `TASK_WDT`, `PANIC`, …).
  Present only on the first marker of each boot, so counting `reset` fields counts
  restarts.

`tools/parse_log.py` skips markers.

See [PLAN.md §Format on disk](../PLAN.md#format-on-disk) for the rationale.
