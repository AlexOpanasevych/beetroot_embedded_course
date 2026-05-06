#include <Arduino.h>

#define LDR_PIN     10
#define ADC_MAX     4095
#define VREF        3.3f
#define READ_INTERVAL_MS 100

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for native USB CDC to connect
}

void loop() {
  int raw = analogRead(LDR_PIN);
  float voltage = (raw / (float)ADC_MAX) * VREF;
  // LDR pull-up: more light -> lower resistance -> lower voltage
  float lightPercent = (1.0f - raw / (float)ADC_MAX) * 100.0f;

  uint32_t analogReadMV = analogReadMilliVolts(LDR_PIN);

  Serial.printf("Raw: %4d | Voltage: %.2fV | Light: %.1f%% | AnalogReadMV: %d | exp: %.1f%\n",
                raw, voltage, lightPercent, analogReadMV, ((analogReadMV / (VREF * 1000.0f)) / voltage) * 100.0f);

  delay(READ_INTERVAL_MS);
}
