// SailinGrace-esp32 — boat-side WiFi NMEA-0183 logger
//
// Joins the boat WiFi, connects to the MFD's NMEA-0183-over-TCP feed
// (Phase 0: Lynx B&G at 192.168.0.16:10110), and appends every line to an
// NDJSON file on the SD card, one record per sentence:
//
//   {"ms":12345,"t":"2026-06-19T14:33:21Z","src":"tcp:192.168.0.16:10110","raw":"$GPRMC,..."}
//
// "ms" = millis() since boot (monotonic, always present). "t" = UTC from the
// RTC if fitted, else "" — absolute time is then reconstructed ashore from the
// GPS sentences in the stream (Phase 0 confirmed GPS is present) aligned on ms.
//
// Design notes:
//  - Fixed char buffers, no String in the capture path → no heap fragmentation
//    over a 6-day run.
//  - TCP reconnect with exponential backoff; falls back to the second MFD.
//  - Daily file rotation by UTC date (only meaningful with an RTC).
//  - Free-space floor: stops writing below SD_MIN_FREE_MB rather than fill the
//    card. (Prune-oldest is a Phase-5 hardening item; 6 days of NDJSON is
//    <1 GB on a 32 GB card, so the floor is a backstop, not the normal path.)
//  - LED: heartbeat = capturing · solid = no WiFi · fast blink = no SD.
//
// NOT compile-verified in CI (no toolchain in the authoring env) — run
// `pio run` and a bench soak before trusting it. See PLAN.md Phase 1/3.

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>

#include "config.h"
#include "secrets.h"

// ── State ─────────────────────────────────────────────────────────────
RTC_PCF8523 rtc;
bool        rtcOk     = false;
bool        sdOk      = false;
bool        writePaused = false;            // true when below the free-space floor

WiFiClient  feed;
const char* feedHost  = TCP_HOST;           // current host (flips to fallback on failure)
uint8_t     hostFails = 0;                   // consecutive connect failures on feedHost

File        logFile;
char        currentDate[11] = {0};           // "YYYY-MM-DD" of the open file

char        line[LINE_MAX];                  // accumulates one NMEA sentence
size_t      lineLen   = 0;
bool        lineOverflow = false;

unsigned long lastFlushMs   = 0;
unsigned long lastGuardMs   = 0;
unsigned long lastConnectMs = 0;
unsigned long backoffMs     = TCP_BACKOFF_MIN_MS;
unsigned long lastBlinkMs   = 0;
bool          ledOn         = false;

// ── Time helpers ──────────────────────────────────────────────────────
static void isoNow(char* buf, size_t n) {
  if (rtcOk) {
    DateTime t = rtc.now();
    snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  } else {
    buf[0] = '\0';                           // no RTC → empty; GPS-in-stream fills it ashore
  }
}

static void todayStr(char* buf, size_t n) {
  if (rtcOk) {
    DateTime t = rtc.now();
    snprintf(buf, n, "%04d-%02d-%02d", t.year(), t.month(), t.day());
  } else {
    snprintf(buf, n, "0000-00-00");          // single file when we have no clock
  }
}

// ── SD ────────────────────────────────────────────────────────────────
static bool mountSD() {
  if (!SD.begin(SD_CS)) {
    sdOk = false;
    return false;
  }
  SD.mkdir(LOG_DIR);
  sdOk = true;
  return true;
}

static bool freeSpaceOk() {
  if (!sdOk) return false;
  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  uint64_t freeMB = (total > used) ? (total - used) / (1024ULL * 1024ULL) : 0;
  return freeMB >= (uint64_t)SD_MIN_FREE_MB;
}

// Open (or rotate to) the log file for the current UTC date.
static void openLogForToday() {
  char date[11];
  todayStr(date, sizeof(date));
  if (logFile && strcmp(date, currentDate) == 0) return;   // already open for today

  if (logFile) logFile.close();
  strncpy(currentDate, date, sizeof(currentDate));
  char path[40];
  snprintf(path, sizeof(path), "%s/log_%s.ndjson", LOG_DIR, date);
  logFile = SD.open(path, FILE_APPEND);
  if (logFile) {
    Serial.printf("logging to %s\n", path);
  } else {
    Serial.printf("ERROR: cannot open %s\n", path);
    sdOk = false;                            // force a remount attempt in the guard
  }
}

// ── Record writer ─────────────────────────────────────────────────────
// JSON-escape into out[]; NMEA/AIS payloads are printable ASCII (no quote /
// backslash in practice), but be defensive and drop control chars.
static size_t jsonEscape(const char* src, size_t srcLen, char* out, size_t outMax) {
  size_t o = 0;
  for (size_t i = 0; i < srcLen && o < outMax - 2; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
    else if (c >= 0x20)        { out[o++] = c; }
    // else: control char — skip
  }
  out[o] = '\0';
  return o;
}

static void writeRecord(const char* raw, size_t rawLen) {
  if (!sdOk || writePaused || !logFile) return;

  // static: keep these off the 8 KB loop-task stack. Safe — writeRecord is
  // only ever called from loop() (single-threaded), never reentrantly.
  static char iso[24];
  static char esc[LINE_MAX * 2];
  static char rec[REC_MAX];
  isoNow(iso, sizeof(iso));
  jsonEscape(raw, rawLen, esc, sizeof(esc));

  int n = snprintf(rec, sizeof(rec),
                   "{\"ms\":%lu,\"t\":\"%s\",\"src\":\"tcp:%s:%d\",\"raw\":\"%s\"}\n",
                   millis(), iso, feedHost, TCP_PORT, esc);
  if (n <= 0) return;
  if (n >= (int)sizeof(rec)) n = sizeof(rec) - 1;       // truncated guard
  logFile.write((const uint8_t*)rec, n);
}

// ── WiFi ──────────────────────────────────────────────────────────────
static bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                       // modem-sleep — the main power lever (see PLAN power budget)

  Serial.printf("WiFi: joining %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (!wifiUp() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  // Fallback SSID, if configured and the primary didn't come up.
  if (!wifiUp() && strlen(WIFI_SSID_FALLBACK) > 0) {
    Serial.printf("WiFi: primary failed, trying %s\n", WIFI_SSID_FALLBACK);
    WiFi.begin(WIFI_SSID_FALLBACK, WIFI_PASSWORD_FALLBACK);
    start = millis();
    while (!wifiUp() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) delay(250);
  }

  if (wifiUp()) Serial.printf("WiFi: connected, IP %s\n", WiFi.localIP().toString().c_str());
  else          Serial.println("WiFi: not connected (will retry)");
}

// ── Feed (TCP) ────────────────────────────────────────────────────────
// Connect with backoff; alternate to the fallback MFD after repeated misses.
static void ensureFeed() {
  if (feed.connected()) return;
  if (millis() - lastConnectMs < backoffMs) return;
  lastConnectMs = millis();

  if (hostFails >= 3) {                       // this host keeps failing — try the other MFD
    feedHost = (strcmp(feedHost, TCP_HOST) == 0) ? TCP_HOST_FALLBACK : TCP_HOST;
    hostFails = 0;
    Serial.printf("feed: switching to %s\n", feedHost);
  }

  Serial.printf("feed: connecting %s:%d\n", feedHost, TCP_PORT);
  if (feed.connect(feedHost, TCP_PORT)) {
    Serial.println("feed: connected");
    backoffMs = TCP_BACKOFF_MIN_MS;
    hostFails = 0;
    lineLen = 0; lineOverflow = false;        // start clean on a fresh socket
  } else {
    hostFails++;
    backoffMs = min(backoffMs * 2, (unsigned long)TCP_BACKOFF_MAX_MS);
    Serial.printf("feed: connect failed (backoff %lums)\n", backoffMs);
  }
}

// Drain available bytes, split into lines, write each complete line.
static void pumpFeed() {
  while (feed.available()) {
    int ci = feed.read();
    if (ci < 0) break;
    char c = (char)ci;
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) writeRecord(line, lineLen);
      lineLen = 0; lineOverflow = false;
    } else if (c == '\r') {
      // ignore — handled by the \n
    } else {
      if (lineLen < LINE_MAX - 1) line[lineLen++] = c;
      else lineOverflow = true;               // pathological line — drop it, don't smear into the next
    }
  }
}

// ── LED status ────────────────────────────────────────────────────────
static void updateLed() {
  unsigned long now = millis();
  if (!sdOk) {                                // fast blink — SD problem (most urgent)
    if (now - lastBlinkMs >= 120) { ledOn = !ledOn; digitalWrite(LED_BUILTIN, ledOn); lastBlinkMs = now; }
  } else if (!wifiUp()) {                      // solid on — no WiFi
    digitalWrite(LED_BUILTIN, HIGH);
  } else {                                     // heartbeat — alive & capturing
    unsigned long phase = now % 2000;
    digitalWrite(LED_BUILTIN, phase < 60 ? HIGH : LOW);
  }
}

// ── Arduino entry points ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nSailinGrace-esp32 logger booting");
  pinMode(LED_BUILTIN, OUTPUT);

  Wire.begin();
  if (rtc.begin()) {
    // initialized() is the documented PCF8523 "clock is running / has been
    // set" check. An unset clock is treated as no clock so we never log a
    // bogus wall-time — GPS-in-stream gives absolute time ashore regardless.
    if (rtc.initialized()) {
      rtcOk = true;
      Serial.println("RTC: ok");
    } else {
      Serial.println("RTC: present but unset — timestamps empty until set (run a set-time sketch)");
    }
  } else {
    Serial.println("RTC: not found — timestamps empty; reconstruct from GPS ashore");
  }

  // Mount SD (retry — the card or contacts can be slow on cold boot).
  for (int i = 0; i < 5 && !mountSD(); i++) { Serial.println("SD: mount retry"); delay(500); }
  if (sdOk) { Serial.println("SD: mounted"); openLogForToday(); }
  else      Serial.println("SD: FAILED — fast-blink; fix card and reboot");

  connectWifi();
  lastGuardMs = millis();
}

void loop() {
  if (wifiUp()) {
    ensureFeed();
    if (feed.connected()) pumpFeed();
  }

  // Periodic flush — bounds how much is lost on a power cut.
  if (sdOk && logFile && millis() - lastFlushMs >= FLUSH_INTERVAL_MS) {
    logFile.flush();
    lastFlushMs = millis();
  }

  // Guard: reconnect WiFi, remount SD, roll the date, check free space.
  if (millis() - lastGuardMs >= GUARD_INTERVAL_MS) {
    lastGuardMs = millis();

    if (!wifiUp()) { Serial.println("guard: WiFi down — reconnecting"); connectWifi(); }
    if (!sdOk)     { Serial.println("guard: SD down — remounting"); if (mountSD()) openLogForToday(); }

    if (sdOk) {
      openLogForToday();                       // rotate if the UTC date changed
      bool ok = freeSpaceOk();
      if (!ok && !writePaused) { writePaused = true;  Serial.println("guard: below free-space floor — PAUSING writes"); }
      if (ok &&  writePaused)  { writePaused = false; Serial.println("guard: free space recovered — resuming writes"); }
    }
  }

  updateLed();
  delay(2);                                    // yield to the WiFi/TCP stack
}
