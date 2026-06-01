# Connecting to Lynx instrument data — field notes

Captured 2026-05-31, dockside at Beverly (BYC), laptop running SailinGrace
in Docker under WSL2. This is the record of how we found the boat's live
NMEA feed and got it into the app, plus the gotchas, so next time is a
checklist instead of an investigation.

> **Credentials are NOT stored here.** The boat WiFi is WPA2-Personal; the
> passphrase is held by the crew. Don't commit it to the repo.

---

## TL;DR — the working path

1. Join WiFi **`lynx-instruments`** (WPA2). Laptop gets `192.168.0.101/24`,
   gateway `192.168.0.1` (an OPNsense firewall).
2. The live feed is **NMEA-0183 over TCP, port `10110`**, served by the B&G
   MFDs at **`192.168.0.15`** (Zeus3S) and **`192.168.0.16`** (richer set —
   adds boat speed, depth, water temp). Use **`.16`**.
3. WSL2 is NAT-isolated and **cannot reach the boat LAN directly.** Bridge it
   with a Windows→WSL push relay (below), or — better for a real deploy —
   switch WSL to **mirrored networking** and connect the container straight to
   `192.168.0.16:10110`.
4. Point SailinGrace's `ingest_url` at the feed (scheme `tcp://` selects the
   NMEA-0183 client) and restart the backend. Verify with
   `/api/sensors/capabilities` and `/api/observations`.

---

## Network topology (as discovered)

### Wired (nav-computer ethernet)
- Laptop `Ethernet 3` = `10.45.211.35/24`, gateway `10.45.211.233` (a
  **TP-Link** router — this is the "tp-link" with its own password).
- **No instrument data on the wire.** The only device on that segment is the
  TP-Link itself. The cable is a management/uplink LAN, not the N2K bridge.

### `lynx-instruments` WiFi — the data network (`192.168.0.0/24`)
- Gateway `192.168.0.1` = **OPNsense** firewall (HTTP/HTTPS admin on 80/443;
  Hyper-V NIC MAC, so likely a VM).
- B&G / Navico devices (OUI `00:0E:91`): `.2`, `.10`, `.11`, `.15`, `.16`,
  plus `.20`/`.200`.
  - **`.15`** = **Zeus3S** MFD (MAC ends `71-A6`, matches the `Zeus3S 71a6` AP).
  - **`.16`** = second B&G device, carries the **fullest** instrument set.
- Data service: **TCP `10110`** open on `.15` and `.16` (NMEA-0183-over-TCP,
  the de-facto standard the SailinGrace NMEA client speaks). Web UIs on 80.

### Other WiFi networks (surveyed, not used)
| SSID | Result |
|---|---|
| `lynx-navigator` (`192.168.20.0/24`) | Joined; **nothing on TCP 80/10110**. Nav PC (Expedition) was off or outputs via UDP/non-standard port — *not re-checked for UDP broadcast*. |
| `Zeus3S 71a6` | **Did not associate** with the boat passphrase — the MFD's own AP has a separate key. Redundant anyway (= `.15`). |
| `VHF_16_024` | Separate `192.168.0.0/24`; only a gateway web admin on `.1` (80/443). **No NMEA feed found.** |

**Conclusion:** `lynx-instruments` `.16` is the single complete source
(wind, boat speed, depth, heel, GPS, AIS). Nothing else beat it.

---

## The feed (sentence inventory, ~120 s capture)

Both MFDs emit the full backbone. Rates approximate:

| Sentence | Data | Rate |
|---|---|---|
| `IIHDG` | heading (+14°W variation) | ~10 Hz |
| `WIMWV` | apparent + true wind | 2 Hz |
| `WIMWD` | wind direction (true/mag) | 1 Hz |
| `SDVHW` | boat speed through water | 1 Hz |
| `SDDBT` / `SDDPT` | depth | 1 Hz |
| `SDMTW` | water temp | 1 Hz |
| `IIXDR` | heel / trim / baro / rudder | 1 Hz |
| `GNGGA` `GPRMC` `GPVTG` `GPGLL` | GPS fix / COG / SOG | 1 Hz |
| `GNGSA` `GNGSV` | satellites | — |
| `!AIVDM` | AIS targets | ~3.6 Hz |
| `GPAPB` `GPBOD` `GPRMB` `GPXTE` | waypoint / autopilot nav | 1 Hz |

Raw captures saved under [`captures/`](../captures/)
(`lynx_nmea_192.168.0.15_*.log`, `…0.16_*.log`). All sentence types above are
already covered by `backend/services/sensors/nmea0183.py`.

---

## Why WSL2 can't just connect (the core problem)

SailinGrace runs in Docker inside WSL2, which uses **NAT networking** by
default:

- WSL `eth0` = `172.17.6.138/20`, gateway `172.17.0.1` (the Windows host's
  "vEthernet (WSL (Hyper-V firewall))").
- Container sits on a Docker bridge `172.19.0.0/16`, gateway `172.19.0.1`
  (= the WSL host).

Reachability we measured:

| Direction | Works? |
|---|---|
| WSL/container → boat LAN (`192.168.0.x`, `10.45.211.x`) | ❌ NAT — not routed |
| WSL → Windows host (`172.17.0.1:<port>`) | ❌ blocked by the **Hyper-V firewall** |
| **Windows host → into WSL** (`172.17.6.138:<port>`) | ✅ works |
| container → WSL host (`172.19.0.1:<port>`) | ✅ works (intra-WSL) |

So the only open lane is **host → guest**. That dictates a *push* relay:
Windows reads the feed and pushes it into WSL.

---

## Workaround that worked: Windows→WSL push relay

```
B&G .16:10110 ──► [Windows pusher] ──► 172.17.6.138:20110 ──► [WSL bridge] ──► :10110 ──► container (172.19.0.1:10110)
   (read)          Win→boat LAN ✓        Win→WSL ✓                fan-out          intra-WSL ✓
```

**WSL fan-out bridge** (`nmea_bridge.py`, stdlib) — listens on `20110` for the
pusher, fans the stream out to clients on `10110`:

```python
import socket, threading
PUSH_PORT, SERVE_PORT = 20110, 10110
clients, lock = set(), threading.Lock()

def serve():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", SERVE_PORT)); s.listen(8)
    while True:
        c, _ = s.accept()
        with lock: clients.add(c)

def push():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", PUSH_PORT)); s.listen(1)
    while True:
        c, _ = s.accept()
        for line in c.makefile("rb"):
            with lock:
                for cl in list(clients):
                    try: cl.sendall(line)
                    except OSError: clients.discard(cl)
        c.close()

threading.Thread(target=serve, daemon=True).start(); push()
```

**Windows pusher** (PowerShell, run via interop; auto-reconnects) — reads the
MFD and pushes into the bridge:

```powershell
$mfd='192.168.0.16'; $wsl='172.17.6.138'
while ($true) {
  try {
    $up = New-Object Net.Sockets.TcpClient; $up.Connect($mfd, 10110)
    $dn = New-Object Net.Sockets.TcpClient; $dn.Connect($wsl, 20110)
    $us=$up.GetStream(); $ds=$dn.GetStream(); $buf=New-Object byte[] 8192
    while ($true) { $n=$us.Read($buf,0,$buf.Length); if($n -le 0){break}; $ds.Write($buf,0,$n); $ds.Flush() }
  } catch {} finally { $up.Close(); $dn.Close() }
  Start-Sleep 2
}
```

This relay is **session-bound glue** — both processes die on logout/reboot.
Fine for a bench/dockside test, not for race day.

---

## SailinGrace config

`ingest_url` lives in the container at `/app/data/settings.json` and takes
precedence over the `SIGNALK_URL` / `NMEA0183_URL` env vars. Scheme picks the
client: `tcp://`|`udp://`|`serial://` → NMEA-0183, `ws://` → SignalK.

```bash
# set the feed (preserves other keys), then restart to apply (no live reload)
docker compose exec -T sailingrace python -c "import json,pathlib; \
 p=pathlib.Path('/app/data/settings.json'); d=json.loads(p.read_text()); \
 d['ingest_url']='tcp://172.19.0.1:10110'; p.write_text(json.dumps(d,indent=2,sort_keys=True))"
docker compose restart
```

Verify:
```bash
curl -s localhost:8000/api/sensors/capabilities   # source:"nmea0183", available:true, n_samples_seen rising
curl -s localhost:8000/api/observations           # current_position = real boat lat/lon
curl -s localhost:8000/api/wind/estimate          # live UKF TWS/TWD
```
Confirmed working: position `42.5015°N, -70.8417°W`, UKF wind `TWS 5.25 kt /
TWD 313°`, heel + AIS capabilities auto-detected.

> `docker compose restart` keeps the edited `settings.json`. `docker compose
> up --build` / recreate **resets** it (it's in the image layer, not the
> `/data` volume) — re-apply after a rebuild, or move it to env/volume.

---

## The durable fix (recommended for deployment): WSL mirrored networking

Removes the relay entirely — the container reaches the boat LAN directly.
Requires Windows 11 22H2+.

`%USERPROFILE%\.wslconfig`:
```ini
[wsl2]
networkingMode=mirrored
```
Then `wsl --shutdown` (restarts WSL — will end any running session), relaunch,
and set `ingest_url=tcp://192.168.0.16:10110`. No pusher, no bridge.

---

## Gotchas / lessons (the time-wasters)

- **ICMP is blocked** on the nav devices — a ping sweep finds almost nothing.
  Use the host **ARP/neighbor table** (`Get-NetNeighbor`) or a **TCP** scan
  instead; identify vendors by **OUI** (`00:0E:91` = Navico/B&G).
- **`netsh` drives WiFi from WSL** via interop — build a profile XML with the
  PSK, `netsh wlan add profile` + `netsh wlan connect`. SSIDs with spaces need
  `"name=<ssid>"` quoting.
- **WSL2 NAT isolates the container** from the LAN; only host→guest works.
  Don't burn time trying to dial the host from WSL — it's firewalled.
- **`pkill -f <pat>` self-matches the invoking shell** when `<pat>` appears in
  its own argv. Use a regex that doesn't appear literally, e.g.
  `pkill -f 'nmea_bridge[.]py'`.
- **PowerShell `Where-Object {CommandLine -like '*X*'}` self-matches**: the
  querying process's command line contains `X`. Exclude `$PID` when counting
  or killing your own helper processes.
- The relay **self-heals**: dropping WiFi to survey other SSIDs just makes the
  pusher retry; reconnecting `lynx-instruments` resumes the feed with no
  restart.

---

## Next-time checklist

1. Join `lynx-instruments`; confirm laptop on `192.168.0.0/24`.
2. `Test-NetConnection 192.168.0.16 -Port 10110` (or `.15`) → should be open.
3. (If WSL still NAT) start `nmea_bridge.py` in WSL; start the PowerShell
   pusher. (If mirrored networking) skip — go straight to step 4 with
   `tcp://192.168.0.16:10110`.
4. Set `ingest_url`, `docker compose restart`.
5. Verify `/api/sensors/capabilities` → `source:"nmea0183"` and
   `/api/observations` → real position.
