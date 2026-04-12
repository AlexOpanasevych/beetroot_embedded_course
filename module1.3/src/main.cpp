#include <Arduino.h>

#define RED_PIN 4
#define BLUE_PIN 5
#define white_PIN 7

constexpr int FLASH  = 80;   // ms одного спалаху
constexpr int GAP    = 60;   // ms між спалахами
constexpr int PAUSE  = 350;  // ms між парами


void setup() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(white_PIN, OUTPUT);
}

void singleBlink(int pin) {
  digitalWrite(pin, HIGH);
  delay(FLASH);
  digitalWrite(pin, LOW);
  delay(GAP);
}

void doubleBlink(int pin) {
  singleBlink(pin);
  singleBlink(pin);
}

void loop() {
    doubleBlink(RED_PIN);
    doubleBlink(BLUE_PIN);
    singleBlink(white_PIN);
    delay(PAUSE);
}