// Copy this file to secrets.h and fill in real credentials.
// secrets.h is gitignored — never commit boat WiFi passwords to a public repo.
//
//   cp firmware/src/secrets_template.h firmware/src/secrets.h
//   $EDITOR firmware/src/secrets.h

#pragma once

// Boat WiFi network — the MFD's broadcast SSID
#define WIFI_SSID      "REPLACE-WITH-BOAT-SSID"
#define WIFI_PASSWORD  "REPLACE-WITH-BOAT-PASSWORD"

// Optional: a second network to try if the primary doesn't associate
// (useful for shore-power vs at-sea SSID swaps). Leave empty to disable.
#define WIFI_SSID_FALLBACK     ""
#define WIFI_PASSWORD_FALLBACK ""
