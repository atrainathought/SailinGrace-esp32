#!/usr/bin/env python3
"""Replay a captured NMEA log through SailinGrace's parser → InstrumentSamples.

Turns a raw capture into the same `InstrumentSample` rows the live ingest
produces, so a post-trip log replays through the routing pipeline exactly as
if it had arrived live. Reads either format:

  * the ESP32 logger's NDJSON  — {"ms":..,"t":"<iso|>","src":"..","raw":"$.."}
  * a raw timestamped log       — "2026-05-31T13:27:18Z $GPRMC,..." (the
                                  discovery-capture format)

Output: CSV of InstrumentSamples (one row per coalesced sample, ~1 Hz).

Needs the SailinGrace repo importable (for NMEA0183Parser + pynmea2). Point at
it with --sailingrace PATH or $SAILINGRACE_REPO (default: ../SailinGrace), and
run with a Python that has pynmea2 (e.g. SailinGrace's venv).

Examples:
  parse_log.py ../data/discovery/lynx_nmea_192.168.0.16_2026-05-31.log -o out.csv
  parse_log.py /sd/log_2026-06-19.ndjson --sailingrace ~/SailinGrace --magvar 14
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from datetime import datetime
from pathlib import Path

SAMPLE_COLS = [
    "t", "bsp_kt", "hdg_deg", "aws_kt", "awa_deg", "heel_deg", "pitch_deg",
    "lat", "lon", "sog_kt", "cog_deg",
]


def add_sailingrace_to_path(p: str) -> None:
    repo = Path(p).expanduser().resolve()
    if not (repo / "backend").is_dir():
        sys.exit(f"error: {repo} is not the SailinGrace repo (no backend/). "
                 f"Pass --sailingrace or set SAILINGRACE_REPO.")
    sys.path.insert(0, str(repo))


def parse_iso(s: str) -> float | None:
    """ISO-8601 (with optional 'Z' and fractional seconds) → unix seconds."""
    s = s.strip()
    if not s:
        return None
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        return datetime.fromisoformat(s).timestamp()
    except ValueError:
        return None


def iter_records(path: str):
    """Yield (t_unix_or_None, raw_sentence) from NDJSON or a raw timestamped log."""
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            stripped = line.lstrip()
            if stripped.startswith("{"):                       # ESP32 NDJSON
                try:
                    rec = json.loads(stripped)
                except json.JSONDecodeError:
                    continue
                raw = rec.get("raw", "")
                if raw:
                    yield (parse_iso(rec["t"]) if rec.get("t") else None), raw
            else:                                               # "ISO <sentence>" or bare sentence
                parts = line.split(None, 1)
                if len(parts) == 2 and parts[1][:1] in "$!":
                    yield parse_iso(parts[0]), parts[1]
                elif line[:1] in "$!":
                    yield None, line


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logfile", help="captured log (NDJSON or raw timestamped)")
    ap.add_argument("-o", "--out", default="-", help="output CSV path (default: stdout)")
    ap.add_argument("--sailingrace",
                    default=os.environ.get("SAILINGRACE_REPO", "../SailinGrace"),
                    help="path to the SailinGrace repo (default ../SailinGrace)")
    ap.add_argument("--magvar", type=float, default=None,
                    help="magnetic variation °W for M→T (default: parser default; Lynx ≈ 14)")
    args = ap.parse_args()

    add_sailingrace_to_path(args.sailingrace)
    try:
        from backend.services.sensors.nmea0183 import NMEA0183Parser, NMEACoalescer
    except ImportError as e:
        sys.exit(f"error: cannot import SailinGrace parser ({e}). "
                 f"Run with a Python that has pynmea2 (e.g. SailinGrace's venv).")

    parser = NMEA0183Parser(args.magvar) if args.magvar is not None else NMEA0183Parser()
    coalescer = NMEACoalescer()

    out = sys.stdout if args.out == "-" else open(args.out, "w", newline="")
    writer = csv.writer(out)
    writer.writerow(SAMPLE_COLS)

    n_lines = n_parsed = n_samples = 0
    last_t: float | None = None
    for t, raw in iter_records(args.logfile):
        n_lines += 1
        update = parser.feed(raw)
        if update is None:
            continue
        n_parsed += 1
        if t is not None:
            update.t = t
            last_t = t
        elif update.t is None and last_t is not None:
            update.t = last_t                                  # carry last known time for untimed lines
        for s in coalescer.feed(update):
            writer.writerow([getattr(s, c, None) for c in SAMPLE_COLS])
            n_samples += 1

    if out is not sys.stdout:
        out.close()
    print(f"parse_log: {n_lines} lines, {n_parsed} parsed sentences, "
          f"{n_samples} samples → {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
