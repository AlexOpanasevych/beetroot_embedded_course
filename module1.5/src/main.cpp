#include <Arduino.h>

#define BUTTON 46
#define DEBOUNCE_MS 50

volatile int16_t counter = 0;
volatile bool flag = false;
volatile uint32_t last_press = 0;

void IRAM_ATTR reaction() {
  uint32_t now = millis();
  if (now - last_press >= DEBOUNCE_MS) {
    last_press = now;
    counter++;
    flag = true;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT);
  attachInterrupt(digitalPinToInterrupt(BUTTON), reaction, FALLING);
}

void loop() {
  if (flag) {
    flag = false;
    Serial.printf("\nButton Pressed! Count: %d\n", counter);
  }

  Serial.print("HELLO ");
  delay(250);
}