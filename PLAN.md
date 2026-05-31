# SailinGrace-esp32 — Build Plan

Last updated: 2026-05-31

## Goal

Capture the boat's WiFi-broadcast instrument data for the full duration
of the 2026 Newport-Bermuda race (4–6 days at sea) onto an SD card, on
battery, with zero per-day intervention. Pull the SD afterwards, replay
through the SailinGrace pipeline ashore.

## Non-goals

- Real-time routing or any compute (that's the laptop running SailinGrace)
- Real-time relay to a downstream consumer (that's `SailinGrace-pi`)
- Running signalk-server (no room in 520 KB SRAM)
- Sending data anywhere off-boat (no cellular, no satellite, no MQTT)
- Live display (no screen — debug LED only)

If any of those become requirements, the answer is "use SailinGrace-pi
on a Pi 4 or Pi Zero 2 W instead."

## What we know about Lynx (from the 1hz branch + Pi discovery work)

| Fact | Source |
|---|---|
| Instrument data is on a **password-protected boat WiFi network** ("Lynx MFD" was the working name in `join_wifi.sh` examples) | `SailinGrace-pi/docs/install_rpi.md` §"Joining a password-protected boat WiFi" |
| N2K backbone access is **unconfirmed** — may or may not be tappable physically | `project-pi-relay-only` memory note |
| Expected wire formats over WiFi (one of): NMEA 0183 UDP broadcast (B&G GoFree / Garmin / Raymarine MFDs), NMEA 0183 TCP server (Expedition's "NMEA Output"), or SignalK over WebSocket (less common on production MFDs) | `SailinGrace/backend/services/sensors/nmea0183_client.py` URL docs |
| Common UDP ports to probe: **2000, 10110, 50000**. TCP: localhost + gateway, also subnet sweep | `SailinGrace/scripts/discover_signals.py` |
| Expected NMEA 0183 sentences: **RMC** (GPS+SOG+COG), **MWV** (AWS+AWA), **MWD** (TWS+TWD), **VHW** (BSP+HDG), **HDG** (heading+variation) | `SailinGrace/backend/services/sensors/nmea0183.py` |
| Expected SignalK paths (if WS): `navigation.position`, `navigation.speedOverGround`, `environment.wind.angle{Apparent,True}`, `environment.wind.speed{Apparent,True}`, `navigation.headingMagnetic`, etc. | `SailinGrace/backend/services/sensors/signalk.py` |
| Single-radio constraint: the ESP32's one WiFi radio is *client* (joining boat network). No AP, no second SSID. | ESP32 hardware |

**The critical unknown**: which protocol Lynx actually broadcasts. This
determines whether the firmware is 50 lines (NMEA 0183 UDP) or 300
lines (SignalK WS). The signal-discovery step on the boat (Phase 0
below) resolves this before any ESP32 firmware is written.

## Hardware

Target board: **Adafruit ESP32-S3 Feather** ($20) — the picky parts
(USB-C native, SD slot via Adalogger FeatherWing, JST battery
connector, well-documented Arduino + PlatformIO support, 8 MB PSRAM
on the Reverse TFT variant) line up. Plain ESP32 DevKit ($8) also works
but needs more breadboarding.

| Part | Notes | Cost |
|---|---|---|
| Adafruit ESP32-S3 Feather | Native USB-C, JST 2-pin battery, 8 MB flash | $20 |
| Adalogger FeatherWing | SPI SD slot + RTC (DS3231-equivalent PCF8523) | $9 |
| 32 GB microSD (SanDisk industrial-rated preferred) | NDJSON capture for 6 days uses <1 GB; sized for headroom | $8 |
| **OR** all-in-one alternative: LilyGo T-Display ESP32-S3 with onboard SD | If you want a status screen | $25 |
| 10 000 mAh USB power bank | Covers 6 days at 0.2 W average with ~80% margin | $25 |
| Pelican 1015 Micro Case or equiv | Watertight, snorkels through a hatch | $20 |
| Misc: silicone caulk, zip ties | | $5 |
| **Total** | | **~$90** |

If standalone-from-house-bus matters: a single 18650 cell + holder
(~$8) instead of the USB bank still gives 2–3 days; a 3-cell holder
($15 + cells) goes to 6+ days. USB bank is friendlier for non-electrical
people on the boat.

## Software architecture

```
┌──────────────────────────────────────────────────────────┐
│ ESP32-S3 (Arduino framework via PlatformIO)              │
│                                                          │
│  setup():                                                │
│   - boot LED blink                                       │
│   - mount SD (retry loop)                                │
│   - WiFi.begin(SSID, PASS) → wait for IP                 │
│   - load config: capture mode (UDP / TCP / WS / AUTO)    │
│                                                          │
│  loop():                                                 │
│   ┌─── mode = UDP_NMEA ────────────────────────────────┐ │
│   │  WiFiUDP.parsePacket()                             │ │
│   │  → append line to /sd/log_YYYY-MM-DD.ndjson        │ │
│   └────────────────────────────────────────────────────┘ │
│   ┌─── mode = TCP_NMEA ────────────────────────────────┐ │
│   │  WiFiClient.read() one line at a time              │ │
│   │  → append to /sd/log_*.ndjson                      │ │
│   │  reconnect with backoff on close                   │ │
│   └────────────────────────────────────────────────────┘ │
│   ┌─── mode = SIGNALK_WS ──────────────────────────────┐ │
│   │  ArduinoWebsockets receives frame                  │ │
│   │  → parse JSON delta with ArduinoJson streaming     │ │
│   │  → append entire delta as one NDJSON line          │ │
│   └────────────────────────────────────────────────────┘ │
│                                                          │
│   periodic (every 60 s):                                 │
│   - check WiFi.status(), reconnect if STA_DISCONNECTED   │
│   - check SD writable, remount if not                    │
│   - rotate file if UTC date changed                      │
│   - prune oldest files if SD <100 MB free                │
│                                                          │
│   periodic (every 10 s):                                 │
│   - heartbeat LED toggle (visible = alive)               │
└──────────────────────────────────────────────────────────┘
```

### Libraries

- **PlatformIO** as the build system (one-command flash, library
  resolver, board configs)
- `WiFi.h` (built-in)
- `WiFiUDP.h` / `WiFiClient.h` (built-in) — NMEA paths
- `ArduinoWebsockets` by Gil Maimon — SignalK WS path
- `ArduinoJson` v7 by Bblanchon — streaming JSON parse for SignalK
- `SD.h` + `SPI.h` (built-in) — SD card writes
- `RTClib` by Adafruit — only if Adalogger RTC is fitted

### Format on disk

NDJSON, one line per captured event, daily rotation:

```
/sd/log_2026-06-19.ndjson
/sd/log_2026-06-20.ndjson
...
```

Each line:

```json
{"t": "2026-06-19T14:33:21.123Z", "src": "udp:10110", "raw": "$GPRMC,..."}
{"t": "2026-06-19T14:33:21.456Z", "src": "ws:signalk", "delta": {...}}
```

Single line-oriented format → trivially streamable, `grep`-able,
matches what the SailinGrace `capture_*.py` scripts produce. The
post-trip ingest tool (`tools/parse_log.py`) feeds this back through
`SailinGrace/backend/services/sensors/nmea0183.py` or `.signalk.py`.

### Configuration

Compile-time `secrets.h` (gitignored) holds the WiFi credentials.
Runtime-tunable knobs live in `config.h`:

```cpp
#define CAPTURE_MODE   MODE_UDP_NMEA  // or MODE_TCP_NMEA, MODE_SIGNALK_WS, MODE_AUTO
#define UDP_PORTS      {2000, 10110, 50000}  // ports to listen on for UDP_NMEA
#define TCP_HOST       "192.168.4.1"          // when MODE_TCP_NMEA
#define TCP_PORT       10110
#define SIGNALK_URL    "ws://192.168.4.1:3000/signalk/v1/stream?subscribe=self"
#define SD_MIN_FREE_MB 100
```

`MODE_AUTO` would try in order: UDP → TCP → WS, picking the first that
yields traffic in 30 s. Optional, can ship without it.

## Phased build

### Phase 0 — Resolve the protocol unknown (BLOCKER, on Lynx)

**See [`docs/phase0.md`](docs/phase0.md) for the step-by-step boat-day
procedure** (join WiFi, scan, capture, verify GPS presence, lock in
the firmware path).

Before any ESP32 work, run `discover_signals.py` from `SailinGrace-pi`
on the boat. This tells us:

- WiFi SSID + password that actually works (already known if Pi has
  joined successfully)
- Which channel emits live data: `udp:2000` / `udp:10110` / `tcp:...` /
  `signalk:ws://...`
- What sentences/paths are present, at what rates
- **Whether GPS is on the WiFi feed** (RMC sentence or
  `navigation.position` path) — open question; the SailinGrace data
  model is GPS-ready, but we don't yet know if Lynx's MFD broadcasts
  position over the WiFi network or holds it for itself
- Output: `data/discovery/<ts>_summary.json` from `discover_signals.py
  capture`

**No ESP32 firmware work until this is done.** The protocol decision
forks the build path 5×.

### Phase 1 — Bench bring-up (1 evening)

- PlatformIO project skeleton (`platformio.ini` for ESP32-S3 Feather)
- "hello world": blink LED, mount SD, write a known file, dismount
- WiFi.begin → IP printed to serial
- Smoke test: 100 lines to SD card, read back, verify

**Deliverable:** `firmware/src/main.cpp` that boots, joins WiFi, writes
one heartbeat file every 10 s. No capture logic yet.

**Done when:** the chip survives an overnight bench run with the
heartbeat file growing as expected.

### Phase 2 — NMEA 0183 UDP capture (1 weekend — if Phase 0 found UDP)

- Implement `WiFiUDP.parsePacket()` loop
- Sentence checksum validation (`$` to `*` XOR per NMEA 0183 spec)
- Daily NDJSON rotation by UTC date
- Free-space guard (stop writing if <100 MB free)
- Periodic WiFi reconnect

**Test:** point the ESP32 at the boat WiFi at the dock, capture for
2 hours, pull SD, verify NDJSON parses cleanly with `tools/parse_log.py`
and the sentence rates match what `discover_signals.py capture` saw.

### Phase 3 — NMEA 0183 TCP fallback (1 day — if Phase 0 found TCP)

Same as Phase 2 but `WiFiClient` instead of UDP. Slightly more code
because TCP needs explicit connect/disconnect and line buffering across
packet boundaries.

### Phase 4 — SignalK WebSocket capture (1 weekend — if Phase 0 found SK)

This is the most complex path:

- `ArduinoWebsockets` client to the SignalK delta stream
- `ArduinoJson` streaming parser (the 512 KB SRAM forbids buffering
  full deltas; must use `DeserializationOption::Filter` or stream
  directly to the SD write buffer)
- Handle fragmented WS frames
- Reconnect on close with exponential backoff (1 s → 30 s)
- TLS for `wss://` if needed (ESP32 has hardware crypto; works but
  doubles the binary size)

**Done when:** WS stream captures cleanly through a forced router
reboot (client reconnects without restart).

### Phase 5 — Production hardening (1 day)

- Watchdog: ESP32 task watchdog set to reset on a 60 s hang
- Brownout: enable the brownout detector; on undervoltage, flush SD
  and halt cleanly (don't corrupt)
- Battery monitor: if Feather, read VBUS / VBAT pins and log to a
  separate `health.ndjson` (so we can post-trip see when the battery
  dipped)
- LED status code: heartbeat-blink (alive), solid (no WiFi), fast-blink
  (no SD), off (dead)
- Burn the SSID into the binary at build time so `secrets.h` is the
  only thing that varies per-deploy

### Phase 6 — Tools (1 day)

`tools/parse_log.py` — reads the NDJSON capture and converts each line
into an `InstrumentSample` via SailinGrace's existing
`nmea0183.NMEA0183Parser` or `signalk.SampleCoalescer`. Output: a CSV
or another NDJSON suitable for the offline replay pipeline.

`tools/inject_to_sailingrace.py` — takes a captured log file and POSTs
the deltas to a running SailinGrace backend's `/observations` endpoint
so the routing UI can replay the trip.

## Open questions / decisions needed

1. **Repo visibility**: public (like `SailinGrace-pi`) or private?
   Boat-side, no routing IP → can be public. Default: public.
2. **SD format**: FAT32 (8 GB max per volume on Windows, but 32 GB SD
   formatted FAT32 by SD card associations' tool works fine in practice).
   exFAT is finicky on ESP32. Default: FAT32, format with SD Card
   Formatter ahead of time.
3. **Time source**: ESP32 has no RTC. Options:
   - NTP at boot (needs internet — boat WiFi probably LAN-only → fails)
   - Add Adalogger's PCF8523 RTC ($9 → already in BOM)
   - Trust GPS time inside NMEA RMC / SignalK `navigation.datetime`
   - **Default: RTC chip on the Adalogger, with GPS-time fallback in
     post-processing**
4. **License**: MIT (matches `SailinGrace-pi`). Confirm with user.
5. **Push to GitHub now or wait for working firmware?** Default: push
   the planning scaffold now so the repo exists and `PLAN.md` is
   shareable; firmware lands later.

## Risks

| Risk | Mitigation |
|---|---|
| Lynx WiFi turns out to be inaccessible (captive portal, MAC allowlist) | Phase 0 catches this before any ESP32 work. Fallback: PiCAN-M on N2K backbone |
| SignalK WS path is the only option, ArduinoJson can't fit deltas | Use filtered deserialization; if still tight, drop to raw frame logging (capture WS frames verbatim, parse ashore) |
| SD card corruption from sudden power loss | Brownout detector + frequent close-reopen of the log file + the existing post-mortem `parse_log.py` already handles truncated last-line NDJSON |
| Battery dies mid-race | 10 Ah USB bank has 80% margin at 0.2 W avg; brownout-halt is graceful; user can hot-swap by plugging a fresh bank |
| ESP32 hangs (WiFi stack bug, library bug) | Task watchdog reset; capture resumes within ~10 s. Worst case: lose one minute of data |
| Wrong UTC date in filenames | RTC chip with battery backup; GPS-time second source |

## Testing strategy

1. **Unit**: each module (`sd_writer.cpp`, `nmea_parser.cpp`) has
   Arduino-native test harnesses runnable from PlatformIO `test`
   command. Don't ship without these.
2. **Soak**: 48-hour bench run against `nmea0183_simulator.py` from
   the SailinGrace repo broadcasting UDP on a known port. Pull SD,
   verify nothing is missing, rotation happened correctly.
3. **Boat shakedown**: deploy on Lynx for one day-sail or a weekend
   buoy race. Compare the ESP32's NDJSON against what the laptop's
   SailinGrace captures over the same network. Discrepancies → bugs.
4. **Race**: drop it in a dry bag, hit start, ignore for 6 days.

## Out of scope (don't ask)

- Sending live data to the laptop or anywhere else — that's the
  Pi-relay job
- Visualization on the device — no screen
- Running ML / decision logic on the chip
- Solar charging (could add later; not for v1)
- Multiple ESP32s (mesh, fleet) — one box, one boat, one race
