# Deployment (boat-day)

**Last Updated**: 2026-11-09

## Before the boat

1. Format a high-endurance microSD card FAT32.
2. Flash a **race build** (no bench flags) with the boat passphrase in
   `secrets.h` — [flash.md](flash.md).
3. Bench-verify the actual USB power bank: run the full rig off it for ≥ 12 h
   and confirm it doesn't auto-shut-off at the logger's low draw
   ([PLAN.md §Power architecture](../PLAN.md#power-architecture--usb-bank-chosen)).
4. Charge the bank fully.

## On the boat

1. Insert the SD card, plug in the bank.
2. Read the status LED (no laptop needed):

| LED | Meaning |
|---|---|
| Solid | No WiFi |
| Slow blink, 1 Hz | On WiFi, no TCP feed from the MFD |
| Fast blink, 5 Hz | Feed connected but silent |
| Single pip each second | Data flowing **and being saved** — good to stow |
| Double pip each second | Data flowing but **not saved** (no card, card error, or below the 100 MB free floor) |

3. Stow somewhere central with WiFi signal, in a watertight box.

## After the race

1. Pull the SD card; copy `/logs/*.ndjson` to `data/captures/` (gitignored).
2. Replay ashore:

```bash
~/SailinGrace/.venv/bin/python tools/parse_log.py \
  --sailingrace ~/SailinGrace data/captures/log_0000-00-00.ndjson -o out.csv
```

`{"event":"open","reset":…}` markers record every restart (watchdog, brownout,
power-on) — see [logs.md](logs.md).
