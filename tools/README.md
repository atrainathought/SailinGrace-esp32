# Post-trip tools

Python utilities for processing the captured NDJSON logs after a
deployment. Phase 6 work — see [../PLAN.md](../PLAN.md).

- `parse_log.py` — NDJSON → InstrumentSample stream via SailinGrace's
  parsers
- `inject_to_sailingrace.py` — NDJSON → POST /observations on a running
  SailinGrace backend
