// Hardware pin assignments and control tuning for the thermostat.
#pragma once

#include <Arduino.h>

// --- Pins ---
const uint8_t PIN_TEMP_SENSOR = A0;          // Temperature sensor input
const uint8_t PIN_RELAY       = 7;           // Heater/HVAC relay output
const uint8_t PIN_STATUS_LED  = LED_BUILTIN; // On-board LED (D13)

// --- Control ---
const float SETPOINT_C   = 21.0;  // Target temperature
const float HYSTERESIS_C = 0.5;   // Dead band either side of setpoint

// --- Timing ---
const unsigned long SAMPLE_INTERVAL_MS = 1000;

// --- Serial ---
const unsigned long SERIAL_BAUD = 115200;
