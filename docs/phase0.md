# Phase 0 — Signal discovery on Lynx

**Goal:** find out what protocol Lynx's WiFi instrument network actually
broadcasts, so we can lock in the ESP32 firmware path (UDP NMEA, TCP
NMEA, or SignalK over WebSocket — see [`../PLAN.md`](../PLAN.md)).

This is a **prerequisite for writing any ESP32 code**. The three
firmware paths fork 5× in effort (afternoon vs weekend), so we don't
guess.

## What you need

- The boat. Lynx, dock or at sea — both work.
- A Pi with [`SailinGrace-pi`](https://github.com/atrainathought/SailinGrace-pi)
  installed (Pi 4 with PiCAN-M, or any Pi running the relay bundle —
  this phase only uses the WiFi side, the PiCAN HAT is irrelevant here).
- The boat WiFi password (you have it — that's the trigger for this
  whole repo existing).
- A laptop with a USB-C cable to the Pi (browser config UI on
  `http://10.55.0.1:8080` once `setup_pi.sh` has run — see Pi repo).

## What you're trying to answer

1. **Which channel is live?**
   - `udp://0.0.0.0:<port>` — MFD broadcasting NMEA 0183 (B&G GoFree,
     Garmin GMI, Raymarine, etc.). Common ports: **2000, 10110, 50000**.
   - `tcp://<ip>:<port>` — Expedition's "NMEA Output" or a SignalK
     server's NMEA-0183 output. Usually port **10110**.
   - `ws://<ip>:3000/signalk/v1/stream` — a SignalK server somewhere on
     the boat network.
2. **What's in the stream?**
   - For NMEA 0183: which sentences (RMC, MWV, MWD, VHW, HDG) at which
     rates?
   - For SignalK: which paths (`navigation.position`,
     `environment.wind.angleApparent`, `navigation.speedOverGround`,
     ...) at which rates?
3. **Is GPS in there?**
   - This is the one we're least sure of. The MFD broadcast might
     include GPS (RMC sentence / `navigation.position` path) or might
     have it on a separate channel. If the WiFi feed has no GPS, the
     ESP32 logger still works but the post-trip trackline has to come
     from somewhere else (handheld, AIS feed, ship's logbook).

## Procedure

### Step 1 — Join the boat WiFi

From the Pi (over USB-C cable from laptop, or SSH if you have a path
in):

```bash
cd /home/pi/SailinGrace-pi   # or wherever setup_pi.sh installed it

# What networks are visible?
scripts/pi/join_wifi.sh --scan

# Join. Replace SSID + password with the real values.
# (The password file approach is safer than putting it on the command
# line — argv shows up in process listings.)
scripts/pi/join_wifi.sh "Lynx MFD" "the-password"

# Confirm
scripts/pi/join_wifi.sh --status
```

Credentials are stored by NetworkManager in
`/etc/NetworkManager/system-connections/` (root-only). **Never** in any
file inside the repo or the bundle — both are public.

### Step 2 — Quick scan: what's live right now?

```bash
sudo .venv/bin/python scripts/discover_signals.py scan --sweep
```

`--sweep` adds a subnet scan for TCP NMEA servers (Expedition often
runs on the laptop's IP, which can be anywhere on the subnet).

You'll get a ranked report like:

```
[1] udp://0.0.0.0:2000     LIVE   sentences: RMC×5, MWV×8, MWD×3, VHW×6
[2] tcp://192.168.4.1:10110  LIVE   sentences: RMC×5, MWV×8, ...
[3] signalk ws://192.168.4.1:3000  not found
[4] can0   not present (no PiCAN, expected)
```

**Decision rule:**
- Multiple channels live? → pick the one with the most sentences /
  highest rates. Usually UDP wins on convenience.
- UDP only on one channel? → that's your ESP32 firmware path
  (`PLAN.md` Phase 2).
- TCP only? → `PLAN.md` Phase 3.
- SignalK only? → `PLAN.md` Phase 4 (most work).
- Nothing? → Lynx's WiFi probably needs a browser-side login (captive
  portal) or only allows whitelisted MAC addresses. Stop here and
  figure out network access before continuing.

### Step 3 — Capture a real sample (5 minutes)

A `scan` only tells you "stuff arrived." A `capture` records the actual
bytes so you can verify the parse works ashore.

```bash
mkdir -p data/discovery
.venv/bin/python scripts/discover_signals.py capture \
    --duration 300 \
    --out-dir data/discovery
```

Outputs:
- `data/discovery/<timestamp>_<channel>.log` — the raw bytes for each
  live channel
- `data/discovery/<timestamp>_summary.json` — sentence/path rates,
  unparseable counts, anything weird

### Step 4 — Verify GPS is in the stream

This is the question we don't yet have an answer for. From the
capture:

```bash
# NMEA 0183 case
grep -c '$..RMC' data/discovery/*_udp_*.log
grep -c '$..GLL' data/discovery/*_udp_*.log   # alternative GPS sentence
grep -c '$..GGA' data/discovery/*_udp_*.log   # GPS fix data

# SignalK case
grep -c '"path": "navigation.position"' data/discovery/*_signalk_*.log
```

If RMC rate is 1–5 Hz (or `navigation.position` is 1 Hz+), GPS is in
the feed. If it's zero, the MFD is publishing wind+depth but holding
GPS for itself — the ESP32 logger still captures everything else, and
the post-trip trackline comes from a separate source.

### Step 5 — Lock in the firmware path

Copy the summary JSON into the SailinGrace-esp32 repo's `data/discovery/`
(create if it doesn't exist; it's `.gitignored`) and capture the
decision in a one-line note here in `phase0.md`:

```
DECISION: Lynx broadcasts NMEA 0183 over UDP port 2000, ~30 sentences/s
including RMC. Build Phase 2 firmware.
```

(Or "TCP on 192.168.4.1:10110" / "SignalK on ws://...".)

That decision unblocks every other phase.

## Common failure modes

| Symptom | Likely cause | Fix |
|---|---|---|
| `join_wifi.sh` reports `secrets_unavailable` | Bad password, or SSID hidden | Verify with the boat owner; for hidden SSIDs use `nmcli connection modify <name> 802-11-wireless.hidden yes` |
| Pi joins WiFi but `discover_signals.py scan` finds nothing | Captive portal, or MFD WiFi only allows known MAC addresses | Try a browser to the gateway IP first — if it serves a login page, the boat WiFi is captive |
| UDP packets arrive but `unparseable` count is high | Garbled NMEA (loose connection on the MFD side), or unsupported sentence variant | Look at the raw log — if it's binary, it's not NMEA at all. If it's text but malformed, the MFD has a buggy NMEA-out implementation; capture verbatim and parse leniently |
| SignalK WS connects then immediately closes | Auth required (rare on boats), or wrong subscription URL | Try `?subscribe=all` instead of `?subscribe=self`. Try without query string. |
| RMC sentences present but `status = V` (invalid) | GPS has no fix yet | Wait 60 s after MFD power-up. If still V, the GPS antenna is shaded or unplugged — not your problem to fix from the laptop side |

## Why we don't just write all three firmware paths

Each path is real engineering work; testing them against the boat's
WiFi remotely without knowing the protocol means writing
simulator-driven tests that may not match reality. Worse, ESP32 SRAM
is tight (520 KB total) — supporting all three modes in one binary
forces conservative library choices that hurt the path that's
actually used.

One firmware, one mode, one well-tested binary > a Swiss army knife
that's bigger and slower in every direction.

## After Phase 0

→ Update `../PLAN.md` "Open questions / decisions needed" section with
the resolved protocol.
→ Start Phase 1 (bench bring-up) on the ESP32 hardware.
→ Move to Phase 2/3/4 firmware depending on the Phase 0 decision.
