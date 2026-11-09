// SailinGrace-esp32 — boat-side WiFi NMEA-0183 logger
//
// Joins the boat WiFi, connects to the MFD's NMEA-0183-over-TCP feed
// (Phase 0: Lynx B&G at 192.168.0.16:10110), and appends every line to an
// NDJSON file on the SD card, one record per sentence:
//
//   {"boot":7,"ms":12345,"t":"2026-06-19T14:33:21Z","src":"tcp:192.168.0.16:10110","raw":"$GPRMC,..."}
//
// "boot" = boot counter (persisted in NVS, +1 every power-up/reset). "ms" =
// millis() since that boot (monotonic within a boot, resets on reboot), so
// (boot, ms) orders every record even with no RTC. "t" = UTC from the RTC if
// fitted, else "" — absolute time is then reconstructed ashore from the GPS
// sentences in the stream (Phase 0 confirmed GPS is present).
//
// Each time a log file is opened, a marker record (no "raw") is written first:
//
//   {"boot":7,"ms":812,"t":"","event":"open","why":"boot","reset":"TASK_WDT"}
//
// "why" = boot | remount | rotate. "reset" (why the chip last restarted —
// post-trip evidence of watchdog resets / brownouts / power cuts) appears only
// on the first marker of each boot, so counting it counts restarts.
//
// Design notes:
//  - Fixed char buffers, no String in the capture path → no heap fragmentation
//    over a 6-day run.
//  - TCP reconnect with exponential backoff; falls back to the second MFD.
//  - Daily file rotation by UTC date (only meaningful with an RTC).
//  - Free-space floor: stops writing below SD_MIN_FREE_MB rather than fill the
//    card. (Prune-oldest is a Phase-5 hardening item; 6 days of NDJSON is
//    <1 GB on a 32 GB card, so the floor is a backstop, not the normal path.)
//  - LED encodes pipeline state (no laptop / no SD needed to read it):
//      solid = no WiFi · slow blink = no feed · fast blink = feed but no data
//      1 pip/s = data flowing + saving to SD · 2 pips/s = data flowing, not saved
//      (2 pips/s is the expected Visit-1 success state with no card in).
//
// Not built in CI — run `pio run` and a bench soak after changes.
// See PLAN.md Phase 1/3.

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "esp_system.h"

#include "config.h"
#include "secrets.h"

// Task watchdog: reset the chip if loop() stops feeding it for WDT_TIMEOUT_S
// (a wedged WiFi/TCP stack self-recovers via reboot → auto-resume). The IDF
// API changed between core 2.x (IDF 4) and 3.x (IDF 5); guard both.
static void wdtBegin() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_S * 1000U,
    .idle_core_mask = 0,        // don't watch the idle tasks, just our loop
    .trigger_panic = true,      // panic-reset on timeout
  };
  if (esp_task_wdt_init(&cfg) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&cfg);   // core already init'd it — just retime
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);             // subscribe the loop task
}
static inline void wdtFeed() { esp_task_wdt_reset(); }

// ── State ─────────────────────────────────────────────────────────────
RTC_PCF8523 rtc;
bool        rtcOk     = false;
bool        sdOk      = false;
bool        writePaused = false;            // true when below the free-space floor

WiFiClient  feed;
const char* feedHost  = TCP_HOST;           // current host (flips to fallback on failure)
uint8_t     hostFails = 0;                   // consecutive failures on feedHost (no connect, or no data)
bool        sockOpen  = false;               // we have an open socket we haven't accounted for yet
bool        sockGotData = false;             // that socket has delivered at least one byte

File        logFile;
char        currentDate[11] = {0};           // "YYYY-MM-DD" of the open file
size_t      logExpected = 0;                 // bytes the open file should hold (size at open + our writes)
bool        resetLogged = false;             // reset reason already written to a marker this boot
uint8_t     sdQuickRetries = 0;              // fast remount attempts left after an SD error
unsigned long lastSdTryMs = 0;

char        line[LINE_MAX];                  // accumulates one NMEA sentence
size_t      lineLen   = 0;
bool        lineOverflow = false;

unsigned long lastFlushMs   = 0;
unsigned long lastGuardMs   = 0;
unsigned long lastConnectMs = 0;
unsigned long backoffMs     = TCP_BACKOFF_MIN_MS;
unsigned long lastRecordMs  = 0;            // when we last captured a complete sentence (LED "data flowing")
unsigned long lastFeedByteMs = 0;           // last byte (or connect) on the feed socket — half-open detection
unsigned long wifiDownSinceMs = 0;          // 0 = WiFi up; else when it went down (auto-reconnect grace)

uint32_t    bootCount   = 0;                // persisted boot counter (NVS)
const char* resetReason = "UNKNOWN";        // esp_reset_reason() at boot, as a name

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// Bump and persist the boot counter. One NVS write per boot — negligible wear.
static void loadBootCount() {
  Preferences prefs;
  if (prefs.begin("sglog", false)) {
    bootCount = prefs.getUInt("boot", 0) + 1;
    prefs.putUInt("boot", bootCount);
    prefs.end();
  } else {
    Serial.println("WARN: NVS unavailable — boot counter stays 0; records can't be ordered across reboots");
  }
}

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
  // SD.begin() returns true immediately if a drive is still registered, so a
  // remount after a failure must SD.end() first or it never re-inits the card.
  // end() is a no-op when nothing is mounted.
  SD.end();
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

// Tear down after an SD error: close the file, unmount, and flag sdOk=false.
// loop() then retries the remount every SD_QUICK_RETRY_MS (SD_QUICK_RETRIES
// times) so a transient glitch costs seconds, not a full guard interval; the
// guard remains the slow backstop after that.
static void sdFail(const char* why) {
  Serial.printf("SD: %s — unmounting, remounting shortly\n", why);
  if (logFile) logFile.close();
  SD.end();
  sdOk = false;
  currentDate[0] = '\0';                    // force a fresh open after remount
  sdQuickRetries = SD_QUICK_RETRIES;
  lastSdTryMs = millis();
}

static void writeOpenMarker(const char* why);

// Open (or rotate to) the log file for the current UTC date. `why` goes in
// the open marker: "boot", "remount" or "rotate".
static void openLogForToday(const char* why) {
  char date[11];
  todayStr(date, sizeof(date));
  if (logFile && strcmp(date, currentDate) == 0) return;   // already open for today

  if (logFile) logFile.close();
  strncpy(currentDate, date, sizeof(currentDate));
  char path[40];
  snprintf(path, sizeof(path), "%s/log_%s.ndjson", LOG_DIR, date);
  logFile = SD.open(path, FILE_APPEND);
  if (logFile) {
    logExpected = logFile.size();
    Serial.printf("logging to %s (existing: %u bytes)\n", path, (unsigned)logExpected);
    writeOpenMarker(why);
  } else {
    Serial.printf("ERROR: cannot open %s\n", path);
    sdFail("open failed");
  }
}

// Marker record at the head of every file open: ties this file segment to a
// boot and says why it was opened. The chip's reset reason goes only on the
// first marker of the boot, so one restart = one "reset". Written straight to
// SD (it's a file-structure record, not feed data).
static void writeOpenMarker(const char* why) {
  if (!logFile) return;
  char iso[24];
  isoNow(iso, sizeof(iso));
  char reset[40] = "";
  if (!resetLogged) snprintf(reset, sizeof(reset), ",\"reset\":\"%s\"", resetReason);
  char rec[192];
  int n = snprintf(rec, sizeof(rec),
                   "{\"boot\":%lu,\"ms\":%lu,\"t\":\"%s\",\"event\":\"open\",\"why\":\"%s\"%s}\n",
                   (unsigned long)bootCount, millis(), iso, why, reset);
  if (n <= 0 || n >= (int)sizeof(rec)) return;
  if (logFile.write((const uint8_t*)rec, n) != (size_t)n) { sdFail("marker write failed"); return; }
  logExpected += n;
  resetLogged = true;
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
  const bool toSD = sdOk && !writePaused && logFile;
  if (!toSD && !SERIAL_ECHO) return;                    // nothing to do (SERIAL_ECHO is a compile constant)

  // static: keep these off the 8 KB loop-task stack. Safe — writeRecord is
  // only ever called from loop() (single-threaded), never reentrantly.
  static char iso[24];
  static char esc[LINE_MAX * 2];
  static char rec[REC_MAX];
  isoNow(iso, sizeof(iso));
  jsonEscape(raw, rawLen, esc, sizeof(esc));

  int n = snprintf(rec, sizeof(rec),
                   "{\"boot\":%lu,\"ms\":%lu,\"t\":\"%s\",\"src\":\"tcp:%s:%d\",\"raw\":\"%s\"}\n",
                   (unsigned long)bootCount, millis(), iso, feedHost, TCP_PORT, esc);
  if (n <= 0) return;
  if (n >= (int)sizeof(rec)) {                          // would truncate → broken JSON line; drop it
    Serial.printf("WARN: record too long (%d B), dropped\n", n);
    return;
  }
  // A short write means the card is gone or erroring — tear down and remount
  // rather than carrying on "saving" into the void. Writes are buffered, so a
  // pulled card usually surfaces at the periodic flush instead (loop()).
  if (toSD) {
    if (logFile.write((const uint8_t*)rec, n) != (size_t)n) sdFail("write failed");
    else logExpected += n;
  }
#if SERIAL_ECHO
  Serial.write((const uint8_t*)rec, n);                 // bench: mirror to the monitor (no SD needed)
#endif
}

// ── WiFi ──────────────────────────────────────────────────────────────
static bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);                       // modem-sleep — the main power lever (see PLAN power budget)
  WiFi.persistent(false);                    // don't wear NVS rewriting creds each connect
  WiFi.setAutoReconnect(true);               // background reconnect on drop — recovers in seconds,
                                             // well before the 60 s guard backstop

  Serial.printf("WiFi: joining %s\n", WIFI_SSID);
  WiFi.disconnect();                         // a retry may find the last fallback attempt still in progress
                                             // (the guard only calls this after WIFI_GRACE_MS of auto-reconnect)
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (!wifiUp() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    wdtFeed();                               // this loop can block ~20 s — keep the WDT fed
    delay(250);
  }

  // Fallback SSID, if configured and the primary didn't come up.
  if (!wifiUp() && strlen(WIFI_SSID_FALLBACK) > 0) {
    Serial.printf("WiFi: primary failed, trying %s\n", WIFI_SSID_FALLBACK);
    WiFi.disconnect();                       // stop the primary attempt, or begin() fails "sta is connecting"
    delay(100);
    WiFi.begin(WIFI_SSID_FALLBACK, WIFI_PASSWORD_FALLBACK);
    start = millis();
    while (!wifiUp() && millis() - start < WIFI_CONNECT_TIMEOUT_MS) { wdtFeed(); delay(250); }
  }

  if (wifiUp()) Serial.printf("WiFi: connected, IP %s\n", WiFi.localIP().toString().c_str());
  else          Serial.println("WiFi: not connected (will retry)");
}

// ── Feed (TCP) ────────────────────────────────────────────────────────
// Connect with backoff; alternate to the fallback MFD after repeated misses.
static void ensureFeed() {
  if (feed.connected()) return;
  if (sockOpen) {                             // the socket we had has closed
    sockOpen = false;
    feed.stop();
    if (!sockGotData) {                       // accepted then closed with no data — that host is failing
      hostFails++;
      backoffMs = min(backoffMs * 2, (unsigned long)TCP_BACKOFF_MAX_MS);
      Serial.printf("feed: closed with no data (backoff %lums)\n", backoffMs);
    }
  }
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
    // hostFails and backoffMs are NOT reset here — only when data actually
    // arrives (pumpFeed). A host that accepts the socket but closes it or stays
    // silent counts as failing, so we back off and rotate to the other MFD.
    sockOpen = true;
    sockGotData = false;
    lastFeedByteMs = millis();                // silence clock starts at connect
    lineLen = 0; lineOverflow = false;        // start clean on a fresh socket
  } else {
    hostFails++;
    backoffMs = min(backoffMs * 2, (unsigned long)TCP_BACKOFF_MAX_MS);
    Serial.printf("feed: connect failed (backoff %lums)\n", backoffMs);
  }
}

// Drain available bytes, split into lines, write each complete line.
static void pumpFeed() {
  bool got = false;
  while (feed.available()) {
    int ci = feed.read();
    if (ci < 0) break;
    got = true;
    char c = (char)ci;
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) {
        lastRecordMs = millis();              // a real sentence arrived — drives the LED "data flowing" state
        writeRecord(line, lineLen);
      }
      lineLen = 0; lineOverflow = false;
    } else if (c == '\r') {
      // ignore — handled by the \n
    } else {
      if (lineLen < LINE_MAX - 1) line[lineLen++] = c;
      else lineOverflow = true;               // pathological line — drop it, don't smear into the next
    }
  }
  if (got) {                                  // this host is genuinely delivering
    lastFeedByteMs = millis();
    sockGotData = true;
    hostFails = 0;
    backoffMs = TCP_BACKOFF_MIN_MS;
  }
}

// Half-open detection: a socket that reports connected but has delivered
// nothing for FEED_SILENCE_MS is dead (MFD rebooted / lost power without
// closing). Drop it so ensureFeed() reconnects — and count it against the host
// so repeated silence rotates to the fallback MFD.
static void checkFeedSilence() {
  if (!feed.connected()) return;
  if (millis() - lastFeedByteMs < FEED_SILENCE_MS) return;
  Serial.printf("feed: no data for %lus — dropping socket\n", FEED_SILENCE_MS / 1000UL);
  feed.stop();
  sockOpen = false;                           // counted here; ensureFeed mustn't count it again
  hostFails++;
}

// ── LED status ────────────────────────────────────────────────────────
// Encodes how far up the pipeline we got, so the board reports its state with
// no laptop and no SD card. Patterns (easy to tell apart at a glance):
//
//   SOLID ON .............. no WiFi (can't join the network)
//   SLOW blink (1 Hz) ..... WiFi up, but the feed (MFD) won't connect
//   FAST blink (5 Hz) ..... feed connected, but no data arriving (silent feed)
//   single PIP / sec ...... ✅ data flowing AND saving to SD — the deploy-happy state
//   double PIP / sec ...... data flowing but NOT saving (no card / SD error)
//                           — this is the EXPECTED success state at Visit 1
//                             (no SD): it means the board reached the real feed.
static void updateLed() {
  const unsigned long now = millis();
  const bool flowing = (lastRecordMs != 0) && (now - lastRecordMs < 3000);
  const bool saving  = sdOk && !writePaused && logFile;

  if (!wifiUp()) {                              // SOLID — no WiFi
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  }
  if (!feed.connected()) {                      // SLOW 1 Hz — on network, no feed
    digitalWrite(LED_BUILTIN, (now % 1000) < 500 ? HIGH : LOW);
    return;
  }
  if (!flowing) {                               // FAST 5 Hz — feed up but silent
    digitalWrite(LED_BUILTIN, (now % 200) < 100 ? HIGH : LOW);
    return;
  }
  // Data flowing: single pip = saving, double pip = not saving (no card / error).
  const unsigned long ph = now % 1000;          // 1 s cycle
  const bool on = saving ? (ph < 50)
                         : (ph < 50) || (ph >= 180 && ph < 230);
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
}

// ── Arduino entry points ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nSailinGrace-esp32 logger booting");
  pinMode(LED_BUILTIN, OUTPUT);

  resetReason = resetReasonName(esp_reset_reason());
  loadBootCount();
  Serial.printf("boot #%lu, last reset: %s\n", (unsigned long)bootCount, resetReason);

  wdtBegin();                                  // before the blocking SD/WiFi bring-up so they can feed it

  Wire.begin();
  if (rtc.begin()) {
    // One-time clock set: build once with -DSET_RTC_EPOCH=$(date -u +%s) to
    // set a fresh PCF8523 to the build-moment UTC. Guarded on !initialized()
    // so it only sets an unset clock and never overwrites a running one.
#ifdef SET_RTC_EPOCH
    if (!rtc.initialized()) rtc.adjust(DateTime((uint32_t)SET_RTC_EPOCH));
#endif
    // initialized() = "clock running / has been set". An unset clock is
    // treated as no clock so we never log a bogus time — GPS-in-stream gives
    // absolute time ashore regardless.
    rtcOk = rtc.initialized();
    if (rtcOk) {
      DateTime n = rtc.now();
      Serial.printf("RTC: ok — %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                    n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
    } else {
      Serial.println("RTC: present but unset — build once with -DSET_RTC_EPOCH; GPS-in-stream covers time");
    }
  } else {
    Serial.println("RTC: not found — timestamps empty; reconstruct from GPS ashore");
  }

  // Mount SD (retry — the card or contacts can be slow on cold boot).
  for (int i = 0; i < 5 && !mountSD(); i++) { Serial.println("SD: mount retry"); delay(500); }
  if (sdOk) { Serial.println("SD: mounted"); openLogForToday("boot"); }
  else      Serial.println("SD: not present — running without card (records not saved to disk; "
                           "LED double-pips once the live feed is flowing)");

  connectWifi();
  lastGuardMs = millis();
}

void loop() {
  wdtFeed();                                   // alive — defer the watchdog reset

  if (wifiUp()) {
    ensureFeed();
    if (feed.connected()) { pumpFeed(); checkFeedSilence(); }
  }

  // Periodic flush — bounds how much is lost on a power cut. Writes are
  // buffered, so this is where a pulled/failed card shows up: after the fsync
  // the on-card file size must cover everything we've written.
  if (sdOk && logFile && millis() - lastFlushMs >= FLUSH_INTERVAL_MS) {
    logFile.flush();
    lastFlushMs = millis();
    size_t onCard = logFile.size();
    if (onCard < logExpected) {
      Serial.printf("SD: flush short (%u of %u B on card)\n", (unsigned)onCard, (unsigned)logExpected);
      sdFail("flush failed");
    }
  }

  // Fast remount after an SD error (sdFail arms a few quick tries).
  if (!sdOk && sdQuickRetries > 0 && millis() - lastSdTryMs >= SD_QUICK_RETRY_MS) {
    sdQuickRetries--;
    lastSdTryMs = millis();
    if (mountSD()) { sdQuickRetries = 0; Serial.println("SD: remounted"); openLogForToday("remount"); }
  }

  // Track how long WiFi has been down, so the guard leaves the background
  // auto-reconnect alone for WIFI_GRACE_MS before forcing a fresh join.
  if (wifiUp())              wifiDownSinceMs = 0;
  else if (!wifiDownSinceMs) wifiDownSinceMs = millis();

  // Guard: reconnect WiFi, remount SD, roll the date, check free space.
  if (millis() - lastGuardMs >= GUARD_INTERVAL_MS) {
    lastGuardMs = millis();

    if (!wifiUp() && millis() - wifiDownSinceMs >= WIFI_GRACE_MS) {
      Serial.println("guard: WiFi down — reconnecting");
      connectWifi();
    }
    if (!sdOk) { Serial.println("guard: SD down — remounting"); if (mountSD()) openLogForToday("remount"); }

    if (sdOk) {
      openLogForToday("rotate");               // rotate if the UTC date changed
      bool ok = freeSpaceOk();
      if (!ok && !writePaused) { writePaused = true;  Serial.println("guard: below free-space floor — PAUSING writes"); }
      if (ok &&  writePaused)  { writePaused = false; Serial.println("guard: free space recovered — resuming writes"); }
    }
  }

  updateLed();
  delay(2);                                    // yield to the WiFi/TCP stack
}
