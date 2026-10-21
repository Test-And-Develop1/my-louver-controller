// Thermostat firmware entry point (Arduino Nano / ATmega328P).

#include "config.h"

bool heating = false;
unsigned long lastSampleMs = 0;

// TODO: replace with the real sensor driver once the sensor is chosen.
float readTemperatureC() {
  int raw = analogRead(PIN_TEMP_SENSOR);
  return raw * (5.0 / 1023.0) * 100.0;  // Placeholder: LM35-style 10 mV/°C
}

// Bang-bang control with hysteresis to avoid rapid relay cycling.
void updateControl(float tempC) {
  if (!heating && tempC < SETPOINT_C - HYSTERESIS_C) {
    heating = true;
  } else if (heating && tempC > SETPOINT_C + HYSTERESIS_C) {
    heating = false;
  }
  digitalWrite(PIN_RELAY, heating ? HIGH : LOW);
  digitalWrite(PIN_STATUS_LED, heating ? HIGH : LOW);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  Serial.println(F("Thermostat starting"));
}

void loop() {
  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs = now;

  float tempC = readTemperatureC();
  updateControl(tempC);

  Serial.print(F("temp_c="));
  Serial.print(tempC, 1);
  Serial.print(F(" heating="));
  Serial.println(heating ? 1 : 0);
}
