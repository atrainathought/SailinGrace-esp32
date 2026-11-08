#pragma once
// Bench simulator configuration. Pins avoid the Feather HUZZAH32's SD card
// (CS 33, SPI 5/18/19) and LED (13) so the same board can run either
// firmware without rewiring.

#include <driver/gpio.h>

#define N2K_TX_PIN            GPIO_NUM_32   // -> transceiver TX / CTX / D
#define N2K_RX_PIN            GPIO_NUM_27   // <- transceiver RX / CRX / R

// N2K identity. Change SIM_UNIQUE_NUMBER if two sims share a bus.
#define SIM_UNIQUE_NUMBER     424242UL      // 21-bit
#define SIM_PREFERRED_ADDRESS 42
#define SIM_MODEL_SERIAL      "SG-SIM-0001"
#define SIM_SW_VERSION        "0.1.0"
#define SIM_MODEL_VERSION     "0.1.0"

// Profile constants (must match docs/bench_test.md).
#define PROFILE_PERIOD_S      120.0
#define BSP_KN                6.5
#define SOG_KN                7.0
#define DEPTH_M               25.0
#define START_LAT             41.4500
#define START_LON             -71.3200
