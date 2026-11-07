// SailinGrace NMEA 2000 bench sensor simulator.
//
// Transmits a deterministic 120 s profile (see SailinGrace-pi/docs/
// bench_test.md, "Test PGNs and expected SignalK paths") so the Pi relay,
// the laptop sniffer and SailinGrace can all be checked against known values.
//
//   PGN 130306 apparent wind   10 Hz   AWS 10/20 kn, AWA +30/+45/-30 deg
//   PGN 127250 heading (mag)   10 Hz   350 -> 10 deg ramp, wraps at t=60 s
//   PGN 129026 COG/SOG rapid   10 Hz   COG = heading, SOG 7.0 kn
//   PGN 129025 position rapid  10 Hz   from 41.4500 N 71.3200 W along COG
//   PGN 128259 speed thru water 1 Hz   6.5 kn
//   PGN 128267 water depth      1 Hz   25.0 m
//
// The serial log (115200) prints the profile values once a second: that is
// the "script truth" the other nodes are compared with.
//
// BENCH ONLY — never on a boat bus with live instruments / autopilot.

#include "config.h"

// The NMEA2000_esp32 backend reads these before NMEA2000_CAN.h is included.
#define ESP32_CAN_TX_PIN N2K_TX_PIN
#define ESP32_CAN_RX_PIN N2K_RX_PIN

#include <Arduino.h>
#include <math.h>
#include <NMEA2000_CAN.h>   // selects the ESP32 TWAI backend
#include <N2kMessages.h>

static const unsigned long kTxPGNs[] PROGMEM = {
    127250L, 128259L, 128267L, 129025L, 129026L, 130306L, 0};

struct Profile {
  double aws_kn, awa_deg, hdg_deg;
};

// Pure function of the cycle time so every node can predict the value.
static Profile profileAt(double t) {
  Profile p;
  p.aws_kn  = t < 60.0 ? 10.0 : 20.0;
  p.awa_deg = t < 40.0 ? 30.0 : (t < 80.0 ? 45.0 : 330.0);  // 330 = 30 port
  p.hdg_deg = fmod(350.0 + t / 6.0, 360.0);
  return p;
}

static double lat = START_LAT, lon = START_LON;
static uint8_t sid = 0;
static unsigned long next10 = 0, next1 = 0, lastFix = 0;

static void send(tN2kMsg &m) {
  if (!NMEA2000.SendMsg(m)) {
    static unsigned long lastWarn = 0;
    if (millis() - lastWarn > 5000) {   // rate-limit: no ACKer = every send fails
      lastWarn = millis();
      Serial.println("WARN: SendMsg failed (no other node on the bus to ACK?)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("SailinGrace N2K bench simulator — BENCH ONLY");

  NMEA2000.SetProductInformation(SIM_MODEL_SERIAL, 100,
                                 "SailinGrace N2K bench sim",
                                 SIM_SW_VERSION, SIM_MODEL_VERSION);
  // Unique number, device function 130 (PC gateway), class 25
  // (inter/intranetwork device), manufacturer 2046 (reserved/experimental).
  NMEA2000.SetDeviceInformation(SIM_UNIQUE_NUMBER, 130, 25, 2046);
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, SIM_PREFERRED_ADDRESS);
  NMEA2000.ExtendTransmitMessages(kTxPGNs);
  NMEA2000.EnableForward(false);
  NMEA2000.Open();
  lastFix = millis();
}

void loop() {
  NMEA2000.ParseMessages();   // address claim, ISO requests (126996, ...)

  const unsigned long now = millis();
  const double t = fmod(now / 1000.0, PROFILE_PERIOD_S);
  const Profile p = profileAt(t);

  if ((long)(now - next10) >= 0) {
    next10 = now + 100;
    sid = (sid + 1) % 253;

    // Dead-reckon the position along COG at SOG.
    const double dt = (now - lastFix) / 1000.0;
    lastFix = now;
    const double d = KnotsToms(SOG_KN) * dt;                  // metres
    lat += d * cos(DegToRad(p.hdg_deg)) / 111320.0;
    lon += d * sin(DegToRad(p.hdg_deg)) / (111320.0 * cos(DegToRad(lat)));

    tN2kMsg m;
    SetN2kWindSpeed(m, sid, KnotsToms(p.aws_kn), DegToRad(p.awa_deg),
                    N2kWind_Apparent);
    send(m);
    SetN2kMagneticHeading(m, sid, DegToRad(p.hdg_deg));
    send(m);
    SetN2kCOGSOGRapid(m, sid, N2khr_true, DegToRad(p.hdg_deg),
                      KnotsToms(SOG_KN));
    send(m);
    SetN2kLatLonRapid(m, lat, lon);
    send(m);
  }

  if ((long)(now - next1) >= 0) {
    next1 = now + 1000;
    tN2kMsg m;
    SetN2kBoatSpeed(m, sid, KnotsToms(BSP_KN), N2kDoubleNA,
                    N2kSWRT_Paddle_wheel);
    send(m);
    SetN2kWaterDepth(m, sid, DEPTH_M, 0.0);
    send(m);
    Serial.printf("t=%5.1f src=%u aws=%.0fkn awa=%.0f hdg=%.1f "
                  "bsp=%.1f sog=%.1f depth=%.1f lat=%.5f lon=%.5f\n",
                  t, NMEA2000.GetN2kSource(), p.aws_kn, p.awa_deg, p.hdg_deg,
                  BSP_KN, SOG_KN, DEPTH_M, lat, lon);
  }
}
