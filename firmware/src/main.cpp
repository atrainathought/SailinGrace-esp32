// SailinGrace-esp32 — boat-side WiFi data logger
//
// This file is a stub. See ../../PLAN.md for the build plan; Phase 1
// will replace this with a working bench bring-up (LED blink, SD mount,
// WiFi join, heartbeat file).

#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("SailinGrace-esp32 — stub. See PLAN.md.");
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
