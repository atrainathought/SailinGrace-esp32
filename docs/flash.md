# Flashing the firmware

Empty for now — depends on Phase 1 firmware existing. Will document:

1. Install PlatformIO Core (`pip install platformio`)
2. `cp firmware/src/secrets_template.h firmware/src/secrets.h` + fill in
3. `cd firmware && pio run -t upload`
4. `pio device monitor` to watch serial during first boot

See [PLAN.md](../PLAN.md) for the phased build plan.
