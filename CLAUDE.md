# SailinGrace-esp32

Boat-side ESP32 datalogger: joins the boat WiFi, reads the MFD's NMEA-0183-over-TCP
feed, appends NDJSON to a microSD card. Pull the card after the race, replay ashore.
Passive capture only — no relay, no signalk-server, no routing on-chip.

Sibling repo — inherits nothing from `~/SailinGrace` (the routing app, laptop-side) or
`~/SailinGrace-pi` (the live relay appliance). `PLAN.md` is the design document;
`docs/` holds the operational how-to (hardware, flash, deployment, log format).

## Layout

| Path | What it is |
|---|---|
| `firmware/src/main.cpp` | The whole firmware — one translation unit, fixed char buffers, no `String` in the capture path |
| `firmware/src/config.h` | Non-secret tunables. The bench-relevant ones (`TCP_HOST*`, `TCP_PORT`, `SERIAL_ECHO`, `SD_CS`, `FEED_SILENCE_MS`, `LED_BUILTIN`) are `#ifndef`-guarded so a `-D` build flag wins; the rest are plain `#define`s |
| `firmware/src/secrets_template.h` | Copy to `secrets.h` (gitignored) before the first build |
| `firmware-n2ksim/` | Separate PlatformIO project: NMEA 2000 bench sensor simulator for the Pi relay. Bench only, never on a live bus |
| `tools/parse_log.py` | Post-trip replay through SailinGrace's own parser. Needs `--sailingrace PATH` and that repo's venv for `pynmea2` |
| `data/discovery/` | Committed Phase 0 field evidence from *Lynx* (2026-05-31). Real deploy captures go to `data/captures/`, gitignored |

Ignore `firmware/.pio/` and `firmware-n2ksim/.pio/` (build output + libdeps).

## Boards — `firmware/platformio.ini`, `default_envs = esp32doit-devkit-v1`

| env | Board | `SD_CS` | RTC |
|---|---|---|---|
| `esp32doit-devkit-v1` | ELEGOO/DOIT DevKit V1 + wired microSD (VSPI 18/19/23/5) | 5 | none |
| `featheresp32` | Feather HUZZAH32 + Adalogger | 33 | PCF8523 |
| `adafruit_feather_esp32s3` | ESP32-S3 Feather + Adalogger | 10 | PCF8523 |

With no RTC the record's `"t"` field is empty and daily UTC rotation is meaningless —
absolute time is reconstructed ashore from the in-stream GPS, aligned on `"ms"`.

## Build / flash / bench

```bash
cp firmware/src/secrets_template.h firmware/src/secrets.h   # then fill in the PSK
cd firmware && pio run -t upload && pio device monitor

# Bench with no SD card and no boat: echo to serial, point at a replay on this laptop
# (the ESP32 needs the laptop's LAN IP, not 127.0.0.1; set the fallback host too)
PLATFORMIO_BUILD_FLAGS='-DSERIAL_ECHO=1 -DTCP_HOST=\"<laptop-ip>\" -DTCP_HOST_FALLBACK=\"<laptop-ip>\"' pio run -t upload
socat TCP-LISTEN:10110,reuseaddr,fork \
  SYSTEM:'cat ../data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log'
```

## Traps

- **No test suite exists**, despite `PLAN.md` §"Testing strategy" describing PlatformIO
  `test` harnesses over `sd_writer.cpp` / `nmea_parser.cpp` modules never split out. A
  bench soak is the only verification in place.
- **ArduinoJson and ArduinoWebsockets are deliberately absent.** Phase 0 locked the path
  to NMEA-0183 over TCP; the SignalK-WebSocket path was dropped to keep the binary lean.
- **`secrets.h` is gitignored and required to compile.** The template's SSID is not
  secret; the passphrase never enters the repo.

<!-- Drafted 2026-08-25 from repo inspection; updated 2026-11-09. Owner should correct anything wrong. -->
