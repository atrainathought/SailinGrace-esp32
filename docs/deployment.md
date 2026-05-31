# Deployment (boat-day)

Empty for now — depends on Phase 5 firmware. Will document:

1. Format SD card (FAT32, 32 GB)
2. Charge USB power bank fully
3. Flash with the boat's WiFi credentials baked into `secrets.h`
4. Bench-soak test for 24 h before sailing
5. On the boat: insert SD, plug battery, drop in dry bag, confirm
   heartbeat LED, stow somewhere central with WiFi signal
6. After the race: pull SD, run `tools/parse_log.py`

See [PLAN.md](../PLAN.md) for the phased build plan.
