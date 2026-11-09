# Flashing the firmware

**Last Updated**: 2026-11-09

## Prerequisites

- PlatformIO Core (`pip install platformio`).
- Linux: your user in the `dialout` group. The DevKit and HUZZAH32 enumerate as
  `/dev/ttyUSB0` (CP2102 / CP2104).
- A **data** USB cable. A charge-only cable powers the board but the host never
  sees it (`lsusb` shows no CP210x).

## Secrets

```bash
cp firmware/src/secrets_template.h firmware/src/secrets.h   # gitignored; required to compile
```

Fill in the boat WiFi passphrase. The optional `WIFI_SSID_FALLBACK` is tried
when the primary SSID doesn't come up (shore WiFi for bench work).

## Race build

```bash
cd firmware
pio run -t upload            # default env: esp32doit-devkit-v1
pio run -e featheresp32 -t upload   # or name another env (see hardware.md)
pio device monitor           # 115200
```

If an upload stalls at `Connecting…`, hold the board's BOOT button until it
starts writing.

Expected boot log: `SailinGrace-esp32 logger booting`, `boot #N, last reset: …`,
RTC and SD status, then `WiFi: connected`, `feed: connected`, and
`logging to /logs/log_….ndjson`.

On the Arduino-ESP32 3.x core you'll also see
`E (…) task_wdt: … TWDT already initialized` at boot. That is benign — the
firmware falls through to `esp_task_wdt_reconfigure()` with its own timeout.

### One-time RTC set (Feather + Adalogger only)

```bash
PLATFORMIO_BUILD_FLAGS="-DSET_RTC_EPOCH=$(date -u +%s)" pio run -e featheresp32 -t upload
```

Only sets an unset clock; never overwrites a running one. Reflash a normal build
afterwards.

## Bench build (no boat)

Build flags override the committed boat defaults without editing any file:

| Flag | Effect |
|---|---|
| `-DSERIAL_ECHO=1` | Mirror every NDJSON record to serial — works with no SD card |
| `-DTCP_HOST="<ip>"` | Feed host instead of the MFD (`192.168.0.16`) |
| `-DTCP_HOST_FALLBACK="<ip>"` | Second host (default `192.168.0.15`); set it too, or the logger alternates to the MFD address after 3 failed connects |

Replay the real *Lynx* capture from the laptop (needs `socat`):

```bash
socat TCP-LISTEN:10110,reuseaddr,fork \
  SYSTEM:'cat ../data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log'

PLATFORMIO_BUILD_FLAGS='-DSERIAL_ECHO=1 -DTCP_HOST=\"<laptop-ip>\" -DTCP_HOST_FALLBACK=\"<laptop-ip>\"' \
  pio run -t upload && pio device monitor
```

The discovery log's lines carry an ISO timestamp prefix; `cat` sends them as-is,
so `raw` will include it. That is fine for a pipeline check.

A feed that stays connected but sends nothing is dropped after 30 s
(`feed: no data for 30s — dropping socket`) and reconnected.

**Always reflash a race build before the boat** — a bench build points at the
laptop.
