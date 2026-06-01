# SailinGrace-esp32 — Build Plan

Last updated: 2026-06-01

> **Phase 0 RESOLVED (2026-05-31, dockside at BYC).** Lynx broadcasts
> **NMEA-0183 over TCP, port `10110`**, from B&G MFDs at `192.168.0.16`
> (use this — fullest set) and `.15`, on WiFi **`lynx-instruments`** (WPA2,
> `192.168.0.0/24`). GPS **is** in the feed. The firmware path is locked to
> **TCP NMEA 0183 (Phase 3)** — UDP (Phase 2) and SignalK WS (Phase 4) are
> not needed. Full evidence: [`data/discovery/`](data/discovery/)
> (`boat_data_connection.md` + raw `.log` captures).

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

## What we know about Lynx (CONFIRMED — Phase 0 discovery, 2026-05-31)

All from the dockside capture in [`data/discovery/`](data/discovery/);
see `boat_data_connection.md` for the full field notes.

| Fact | Detail |
|---|---|
| **WiFi network** | `lynx-instruments`, WPA2-Personal, subnet `192.168.0.0/24`, gateway `.1` (OPNsense). PSK held by crew — never in repo. |
| **Data source** | **NMEA-0183 over TCP, port `10110`**, served by B&G MFDs at `192.168.0.16` (fullest set — wind, BSP, depth, water temp, heel, GPS, AIS) and `192.168.0.15` (Zeus3S). **Target `.16`.** |
| **Protocol** | TCP NMEA-0183. **Not** UDP broadcast, **not** SignalK WS. (UDP ports 2000/10110/50000 were probed — nothing. No SignalK server found.) |
| **GPS present?** | **Yes** — `GNGGA`/`GPRMC`/`GPGLL`/`GPVTG` @ ~1 Hz, real fix (dockside 42.30 N, 70.50 W). The post-trip trackline comes straight from the feed; no separate GPS source needed. |
| **Sentence inventory** (~120 s capture, approx rates) | `IIHDG` heading+var ~10 Hz · `WIMWV` app/true wind 2 Hz · `WIMWD` wind dir 1 Hz · `SDVHW` boat speed 1 Hz · `SDDBT`/`SDDPT` depth 1 Hz · `SDMTW` water temp 1 Hz · `IIXDR` heel/trim/baro/rudder 1 Hz · `GN/GP` GPS 1 Hz · `!AIVDM` AIS ~3.6 Hz · `GPAPB`/`GPBOD`/`GPRMB`/`GPXTE` waypoint nav 1 Hz. All already covered by `SailinGrace/backend/services/sensors/nmea0183.py`. |
| **Other SSIDs** (surveyed, unused) | `lynx-navigator` (nothing on 80/10110), `Zeus3S 71a6` (MFD's own AP, separate key, = `.15`), `VHF_16_024` (gateway admin only). `.16` was the single complete source. |
| **Single-radio constraint** | The ESP32's one radio is a *client* joining `lynx-instruments`. No AP, no second SSID. |

### Why the ESP32 path is far simpler than the laptop's

The connection notes document an elaborate Windows→WSL push-relay because
SailinGrace runs in **Docker-inside-WSL2**, whose NAT isolates the container
from the boat LAN. **None of that applies here.** The ESP32 is a native WiFi
client on `192.168.0.0/24`, so it connects **directly** to
`192.168.0.16:10110` — no relay, no bridge, no mirrored-networking. The
firmware is just: join WiFi → open TCP socket → read lines → write SD.

## Hardware

Target board: **Adafruit ESP32-S3 Feather** ($20) — the picky parts
(USB-C native, SD slot via Adalogger FeatherWing, JST battery
connector, well-documented Arduino + PlatformIO support, 8 MB PSRAM
on the Reverse TFT variant) line up. Plain ESP32 DevKit ($8) also works
but needs more breadboarding.

| Part | Notes | Cost |
|---|---|---|
| Adafruit ESP32-S3 Feather | Native USB-C, JST 2-pin battery, 8 MB flash | $20 |
| Adalogger FeatherWing | SPI SD slot + PCF8523 RTC — SD + a battery-backed clock in one board (see SD note below) | $9 |
| **High-endurance** 32 GB microSD (SanDisk High Endurance / Industrial, or pSLC) | Continuous 24/7 writes for 6 days kill consumer cards; endurance cards are the reliability item, not the size — 6 days of NDJSON is <1 GB | $12 |
| **OR** all-in-one alternative: LilyGo T-Display ESP32-S3 with onboard SD | If you want a status screen | $25 |
| **20 000 mAh** power — **LiPo on the JST (preferred)** or an **always-on USB bank** | Sized from the budget below; 10 Ah is NOT enough for 6 days. A plain USB bank may auto-shut-off at our low draw — see Power budget | $35 |
| Pelican 1015 Micro Case or equiv | Watertight, snorkels through a hatch | $20 |
| Misc: silicone caulk, zip ties | | $5 |
| **Total** | | **~$110** |

### SD card hardware

- **Interface:** the ESP32-S3 supports *both* SPI and native **SDMMC**
  (SD/SDIO, 1- or 4-bit) via a dedicated controller. SDMMC 4-bit is faster
  and lower energy-per-byte, but needs ~6 dedicated GPIOs and isn't on the
  Adalogger. **Our throughput is ~5–10 KB/s** (NMEA text) — SPI has an order
  of magnitude of headroom, so **use the Adalogger's SPI SD** for simplicity
  and get the RTC in the same board. Reserve SDMMC for a high-rate logger we
  don't have.
- **Card:** must be **high-endurance / industrial** (SanDisk High Endurance,
  Industrial, or pSLC). A consumer card under sustained append-logging will
  wear-fail mid-race — endurance is the spec that matters here, not capacity.
- **Filesystem:** **FAT32** (exFAT is finicky on ESP32). Pre-format with the
  SD Association's formatter.
- **Power/reliability:** an inserted card idles ~5–15 mA and peaks ~50–100 mA
  during writes; with buffered/batched writes its *average* contribution is a
  few mA. Note `SD.end()` does **not** drop the S3 into a low-power state, so
  the card keeps drawing idle current — fine on a USB bank, relevant only if
  you ever battery-optimize. Flush/close frequently and brownout-halt
  cleanly so a power cut only truncates the last NDJSON line.

If standalone-from-house-bus matters, see the power budget below for cell
options. A USB bank is friendliest for non-electrical crew.

## Power budget (logging-only)

> All figures are datasheet/range estimates for ESP32-S3 — **bench-measure
> before trusting them**: USB power meter inline, 1 h run at the real data
> rate. The scaffold's "~0.2 W, 10 Ah = 6 days" was optimistic; a
> continuously-connected WiFi TCP logger can't deep-sleep — it holds the
> socket and receives ~5–10 KB/s, so the radio stays up.

### Average draw

| State | ESP32-S3 average @ 3.3 V | Notes |
|---|---|---|
| WiFi associated, modem-sleep, no traffic | ~20–50 mA | beacon RX only |
| **WiFi STA + continuous TCP RX (the logger)** | **~60–100 mA** | radio wakes per packet; bursts ~300 mA on RX |
| + SD writes (buffered/batched) | +~3–10 mA avg | card idles ~5–15 mA; write peaks 50–100 mA, brief |

So the logger is **~0.25–0.4 W at the chip**. The biggest runtime lever is the
**WiFi radio** (tune DTIM / max modem-sleep); SD is noise by comparison. With
BLE dropped, there's no coexistence overhead — the radio does one job.

### Two gotchas that kill always-on USB-bank rigs

1. **USB banks auto-shut-off at low current.** Most cut output below
   ~50–100 mA (they assume charging finished) — and our ~60–90 mA sits right
   in that zone, so a bank can **silently power off mid-race.** This is a
   bigger deployment risk than capacity. → use a bank with an explicit
   **always-on / trickle / low-current mode** and *verify it on the bench for
   hours*, or avoid the problem entirely (below).
2. **Double conversion wastes ~20–25%:** cells 3.7 V → boost 5 V (~88%) →
   Feather regulator → 3.3 V (~88%). You pay both stages.

### Two power architectures

| Option | Pros | Cons |
|---|---|---|
| USB power bank → USB-C | crew-friendly, hot-swappable, no LiPo handling | auto-shutoff risk; double-conversion loss; quiescent drain over 6 days |
| **LiPo straight to the Feather JST** ⭐ | single conversion (3.7 → 3.3 V), **no auto-off failure mode**, ~20% more runtime/Wh, onboard charger + fuel gauge | LiPo handling/stowage; key the connector so it can't be reversed |

For a logger that must **survive 6 days untouched, a big LiPo on the JST is the
more robust choice** — it removes the single most likely "why did it die?"
cause (the bank switching itself off). Keep a *vetted* always-on USB bank as
the crew-friendly fallback.

### Sizing for a 6-day (144 h) race (assume ~80 mA @ 3.3 V — measure to confirm)

| Source | Est. runtime | Verdict |
|---|---|---|
| 10 000 mAh USB bank | ~4 days | ❌ short |
| 20 000 mAh USB bank (always-on) | ~8 days | ✅ margin |
| 10 000 mAh LiPo on JST | ~6 days | ⚠️ ~no margin |
| 20 000 mAh LiPo on JST | ~11 days | ✅ comfortable |

**Recommendation:** target **20 000 mAh** — preferably a **LiPo on the JST**
(most robust), or an **always-on USB bank** (simplest). Either way: measure
real draw first, then keep **≥30 % headroom** for cold (Li-ion loses capacity
offshore) and cell aging.

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
// Phase 0 locked the path: TCP NMEA 0183 to the B&G MFD. The UDP and
// SignalK modes are kept in the architecture for reuse on other boats,
// but Lynx ships single-mode TCP.
#define CAPTURE_MODE   MODE_TCP_NMEA
#define TCP_HOST       "192.168.0.16"   // B&G MFD, fullest set (.15 is the fallback)
#define TCP_PORT       10110
#define SD_MIN_FREE_MB 100
// WiFi SSID is "lynx-instruments"; PSK lives in secrets.h (gitignored).
```

Single mode, no `MODE_AUTO` — we know the answer for Lynx. (Keep `.15`
as a fallback host if `.16` is ever unreachable; both serve the feed.)

## Phased build

### Phase 0 — Resolve the protocol unknown — ✅ DONE (2026-05-31)

**DECISION: Lynx broadcasts NMEA-0183 over TCP at `192.168.0.16:10110`,
GPS included. Build the Phase 3 (TCP) firmware.** Phases 2 (UDP) and 4
(SignalK WS) are not needed for Lynx.

Evidence in [`data/discovery/`](data/discovery/): `boat_data_connection.md`
(field notes) + raw `lynx_nmea_192.168.0.{15,16}_*.log` captures. Procedure
that produced it is in [`docs/phase0.md`](docs/phase0.md). The build path is
now unblocked.

### Phase 1 — Bench bring-up (1 evening)

- PlatformIO project skeleton (`platformio.ini` for ESP32-S3 Feather)
- "hello world": blink LED, mount SD, write a known file, dismount
- WiFi.begin → IP printed to serial
- Smoke test: 100 lines to SD card, read back, verify

**Deliverable:** `firmware/src/main.cpp` that boots, joins WiFi, writes
one heartbeat file every 10 s. No capture logic yet.

**Done when:** the chip survives an overnight bench run with the
heartbeat file growing as expected.

### Phase 2 — NMEA 0183 UDP capture — ❌ NOT NEEDED (Lynx is TCP)

Kept only as a reference if this rig is reused on a boat that broadcasts
UDP. Skip for Lynx.

### Phase 3 — NMEA 0183 TCP capture (1 day) — ⭐ THE LYNX PATH

This is the firmware to build.

- `WiFiClient` connects to `192.168.0.16:10110` (`.15` fallback).
- Read one line at a time; buffer across packet boundaries (TCP doesn't
  preserve line framing — accumulate until `\n`).
- Optional sentence checksum validation (`$`/`!` to `*` XOR); on a capture
  rig, prefer logging verbatim even if a checksum looks off, and validate
  ashore.
- Daily NDJSON rotation by UTC date; free-space guard (<100 MB → prune).
- Reconnect with backoff (1 s → 30 s) on close — the MFD or WiFi will drop
  occasionally over a 6-day race.

**Test:** replay a real capture as a TCP server and point the ESP32 at it —
`socat TCP-LISTEN:10110,reuseaddr,fork SYSTEM:'cat data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log'`
(or a tiny Python server). Then dockside on Lynx for 2 h; pull SD; confirm
`tools/parse_log.py` parses it and the sentence mix matches the discovery
capture.

### Phase 4 — SignalK WebSocket capture — ❌ NOT NEEDED (Lynx is TCP NMEA)

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

## BLE rebroadcast to a Garmin Quatix — PARKED (not in scope)

> **DECISION (2026-06-01): not pursuing. v1 is logging-only.** A live BLE feed
> is a real-time relay, which is outside this repo's charter (passive logger).
> The investigation below is kept for if it's ever revisited — but it is **not
> on the roadmap** and the power budget above is logging-only.

**Ask (parked):** rebroadcast the captured instruments over BLE so a Garmin
quatix watch can show live boat data on the wrist.

**Feasibility — yes, but it's a two-sided custom build, not plug-and-play.**

- **Connect IQ BLE is *central-role only.*** A watch app (incl. a **Data
  Field**) can scan, pair, and read a peripheral's GATT characteristics, but
  **a CIQ app cannot act as a BLE peripheral / cannot be fed by a standard
  "boat data" profile the watch reads natively.** (Verified against Garmin's
  `Toybox.BluetoothLowEnergy` docs.)
- **quatix 7 *is* a supported device** for that module (API 3.1.0+, Data
  Field context) — so the watch side is possible.
- Therefore the only viable shape:
  - **ESP32 = BLE GATT *peripheral*** exposing a small custom service (e.g.
    one characteristic carrying a compact wind/SOG/depth/heading/position
    frame, notified at ~1 Hz), running *alongside* the WiFi TCP capture
    (radio coexistence).
  - **quatix = a custom Connect IQ Data Field** (Monkey C) that connects to
    that peripheral and renders the fields.
- **Not possible:** emulating a Garmin marine device. The quatix natively
  streams boat data only **from Garmin chartplotters/autopilots over a
  Garmin-proprietary link** — a generic ESP32 can't impersonate that.

**Cost / caveats**
- **Power:** +~20–40 mA average (coexistence) — see the power budget; pushes a
  6-day race to ~30 Ah or a hot-swap. Mitigate by **advertising only when the
  watch app actually wants data** (duty-cycle), not continuously.
- **Throughput:** trivial (a few fields at 1 Hz) — coexistence won't starve
  the logger.
- **Connection limits:** the watch is already BLE-paired to the phone; CIQ BLE
  supports few concurrent connections — test the watch+phone+ESP32 combo early.
- **Effort:** two new deliverables (ESP32 GATT peripheral; CIQ data-field app)
  + pairing UX. Roughly a weekend each, watch-side is the unknown.

**Scope tension (decision needed).** This contradicts the repo's charter —
README/Non-goals say *passive logger, **not a relay**, no live display*. A live
BLE feed is a real-time relay to a wearable. Recommendation: **keep v1 a pure
SD logger** (the race-critical deliverable) and treat BLE rebroadcast as a
**separate v2 experiment on its own firmware build/branch**, so it never bloats
or risks the logger that has to survive 6 days untouched. Confirm before this
goes on the v1 roadmap.

## Open questions / decisions needed

1. **Repo visibility**: public (like `SailinGrace-pi`) or private?
   Boat-side, no routing IP → can be public. Default: public.
2. **SD format**: FAT32 (8 GB max per volume on Windows, but 32 GB SD
   formatted FAT32 by SD card associations' tool works fine in practice).
   exFAT is finicky on ESP32. Default: FAT32, format with SD Card
   Formatter ahead of time.
3. **Time source**: ESP32 has no RTC.
   - NTP at boot — **confirmed unavailable**: `lynx-instruments` is a LAN
     (OPNsense gateway, no internet), so NTP fails.
   - **GPS time is confirmed present** in the feed (`GPRMC` date+time,
     `GNGGA` time @ 1 Hz) — a reliable wall-clock source once the MFD has a
     fix. The raw `.log` captures already carry a host timestamp per line.
   - **Default: Adalogger PCF8523 RTC ($9, already in BOM) for immediate
     boot-time stamping, with GPS time (now known to be in-stream) as the
     authoritative second source in post-processing.**
4. **License**: MIT (matches `SailinGrace-pi`). Confirm with user.
5. **Push to GitHub now or wait for working firmware?** Default: push
   the planning scaffold now so the repo exists and `PLAN.md` is
   shareable; firmware lands later.

## Risks

| Risk | Mitigation |
|---|---|
| ~~Lynx WiFi inaccessible (captive portal, MAC allowlist)~~ | **Resolved** — `lynx-instruments` joined cleanly with the boat PSK, no captive portal, no allowlist. (The MFD's own `Zeus3S 71a6` AP has a separate key, but we don't need it.) |
| ~~SignalK WS the only option, ArduinoJson can't fit deltas~~ | **Moot** — Lynx is NMEA-0183 TCP, no JSON parsing on-chip. |
| MFD/WiFi drops over a 6-day race | TCP reconnect with backoff (Phase 3); capture resumes within seconds. The relay self-heals on reconnect (confirmed in the connection notes). |
| SD card corruption from sudden power loss | Brownout detector + frequent close-reopen of the log file + the existing post-mortem `parse_log.py` already handles truncated last-line NDJSON |
| Battery dies mid-race | 10 Ah USB bank has 80% margin at 0.2 W avg; brownout-halt is graceful; user can hot-swap by plugging a fresh bank |
| ESP32 hangs (WiFi stack bug, library bug) | Task watchdog reset; capture resumes within ~10 s. Worst case: lose one minute of data |
| Wrong UTC date in filenames | RTC chip with battery backup; GPS-time second source |

## Testing strategy

1. **Unit**: each module (`sd_writer.cpp`, `nmea_parser.cpp`) has
   Arduino-native test harnesses runnable from PlatformIO `test`
   command. Don't ship without these.
2. **Soak**: 48-hour bench run. Best fidelity is replaying the **real Lynx
   capture** over TCP (`socat TCP-LISTEN:10110,reuseaddr,fork
   SYSTEM:'cat data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log'`), since
   it has the actual sentence mix/rates/AIS the firmware will see;
   `nmea0183_simulator.py --transport tcp` is the synthetic alternative.
   Pull SD, verify nothing is missing and rotation happened correctly.
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
