// Non-secret runtime configuration for the SailinGrace-esp32 logger.
// WiFi credentials live in secrets.h (gitignored). Everything here is safe
// to commit. Phase 0 locked the data path to NMEA-0183 over TCP.
#pragma once

// ── Data source — Lynx B&G MFDs (Phase 0 discovery) ───────────────────
// .16 carries the fullest instrument set; .15 (Zeus3S) serves the same
// feed and is the fallback if .16 is unreachable.
// #ifndef-guarded so a bench build can override the host via a build flag
// (e.g. PLATFORMIO_BUILD_FLAGS='-DTCP_HOST="192.168.1.155"') without editing
// the committed boat defaults.
#ifndef TCP_HOST
#define TCP_HOST           "192.168.0.16"
#endif
#ifndef TCP_HOST_FALLBACK
#define TCP_HOST_FALLBACK  "192.168.0.15"
#endif
#ifndef TCP_PORT
#define TCP_PORT           10110
#endif

// Echo every captured record to Serial (in addition to / instead of SD).
// Default off for deployment; set -DSERIAL_ECHO=1 for a bench test with no
// SD card — proves the WiFi→TCP→parse→format path on the serial monitor.
#ifndef SERIAL_ECHO
#define SERIAL_ECHO        0
#endif

// ── SD card (Adalogger FeatherWing, SPI) ──────────────────────────────
// Adalogger FeatherWing SD chip-select on the Adafruit ESP32 Feather
// (HUZZAH32) is GPIO 33. (It's pin 5/10 on other Feathers — board-specific.)
#define SD_CS              33
#define LOG_DIR            "/logs"
#define SD_MIN_FREE_MB     100      // stop writing below this floor (never fill the card)

// ── Timing ────────────────────────────────────────────────────────────
#define FLUSH_INTERVAL_MS  2000UL   // fsync cadence — bounds data lost on a power cut
#define GUARD_INTERVAL_MS  60000UL  // periodic WiFi / SD / date-rollover / free-space check
#define TCP_BACKOFF_MIN_MS 1000UL   // reconnect backoff floor
#define TCP_BACKOFF_MAX_MS 30000UL  // reconnect backoff ceiling
#define WIFI_CONNECT_TIMEOUT_MS 20000UL

// ── Watchdog ──────────────────────────────────────────────────────────
// Reset the chip if loop() hangs this long (e.g. a wedged WiFi/TCP stack).
// Must comfortably exceed the worst-case blocking call — connectWifi() can
// block ~40 s (primary + fallback associate), so keep this well above that.
#define WDT_TIMEOUT_S      90

// ── Buffers ───────────────────────────────────────────────────────────
#define LINE_MAX           1024     // longest NMEA line + slack (fixed buffer — no String, no heap churn)
#define REC_MAX            1400     // NDJSON record buffer

// ── Status LED ────────────────────────────────────────────────────────
#ifndef LED_BUILTIN
#define LED_BUILTIN        13       // Adafruit ESP32-S3 Feather red LED
#endif
