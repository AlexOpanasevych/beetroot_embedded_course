#include <Arduino.h>

#define RED_LED_PIN 4
#define BLUE_LED_PIN 5
#define GREEN_LED_PIN 6
#define WHITE_LED_PIN 7
#define BUTTON_PIN 46
#define BOOT_BUTTON_PIN 0

enum class BlinkMode {
  OFF,
  ON,
  BLINK_SLOW,
  BLINK,
  BLINK_FAST,
  BLINK_BY_QUERY,
  BLINK_RANDOM,
};

// --- Debounce state ---
bool lastReading = HIGH;
bool lastStableState = HIGH;
unsigned long lastDebounceTime = 0;
bool lastReadingBack = HIGH;
bool lastStableStateBack = HIGH;
unsigned long lastDebounceTimeBack = 0;
const unsigned long DEBOUNCE_DELAY = 150;

// --- Non-blocking blink state ---
bool ledState = false;
unsigned long lastBlinkTime = 0;

int queryLedIndex = 0;
unsigned long lastTime = 0;
const unsigned long BLINK_INTERVAL = 300;  // ms per LED, adjust to taste


const int QUERY_PINS[] = { RED_LED_PIN, BLUE_LED_PIN, GREEN_LED_PIN, WHITE_LED_PIN };
const int QUERY_COUNT = 4;

BlinkMode currentMode = BlinkMode::OFF;

void setup() {
  randomSeed(esp_random());
  Serial.begin(115200);
  delay(1000);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("Boot OK - Mode: OFF");
}

void setAllLEDs(bool state) {
  digitalWrite(RED_LED_PIN,   state ? HIGH : LOW);
  digitalWrite(BLUE_LED_PIN,  state ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, state ? HIGH : LOW);
  digitalWrite(WHITE_LED_PIN, state ? HIGH : LOW);
}

const char* blinkModeToString(BlinkMode mode) {
  switch (mode) {
    case BlinkMode::OFF:        return "OFF";
    case BlinkMode::ON:         return "ON";
    case BlinkMode::BLINK_SLOW: return "BLINK_SLOW";
    case BlinkMode::BLINK:      return "BLINK";
    case BlinkMode::BLINK_FAST: return "BLINK_FAST";
    case BlinkMode::BLINK_BY_QUERY: return "BLINK_BY_QUERY";
    case BlinkMode::BLINK_RANDOM: return "BLINK_RANDOM";
    default:                    return "UNKNOWN";
  }
}

void cycleMode() {
  switch (currentMode) {
    case BlinkMode::OFF:        currentMode = BlinkMode::ON;         break;
    case BlinkMode::ON:         currentMode = BlinkMode::BLINK_SLOW; break;
    case BlinkMode::BLINK_SLOW: currentMode = BlinkMode::BLINK;      break;
    case BlinkMode::BLINK:      currentMode = BlinkMode::BLINK_FAST; break;
    case BlinkMode::BLINK_FAST: currentMode = BlinkMode::BLINK_BY_QUERY;        break;
    case BlinkMode::BLINK_BY_QUERY: currentMode = BlinkMode::BLINK_RANDOM;   break;
    case BlinkMode::BLINK_RANDOM: currentMode = BlinkMode::OFF;   break;

  }
ledState = false;
  lastBlinkTime = millis();
  queryLedIndex = 0;        // ← add
  lastTime = millis(); // ← add
  setAllLEDs(false);        // ← add, clean slate on mode change
  Serial.print("Mode: ");
  Serial.println(blinkModeToString(currentMode));
}

void previousMode() {
  switch (currentMode) {
    case BlinkMode::OFF:           currentMode = BlinkMode::BLINK_RANDOM;    break;
    case BlinkMode::ON:            currentMode = BlinkMode::OFF;              break;
    case BlinkMode::BLINK_SLOW:    currentMode = BlinkMode::ON;              break;
    case BlinkMode::BLINK:         currentMode = BlinkMode::BLINK_SLOW;      break;
    case BlinkMode::BLINK_FAST:    currentMode = BlinkMode::BLINK;           break;
    case BlinkMode::BLINK_BY_QUERY: currentMode = BlinkMode::BLINK_FAST; break;
    case BlinkMode::BLINK_RANDOM:  currentMode = BlinkMode::BLINK_BY_QUERY; break;
  }
  ledState = false;
  lastBlinkTime = millis();
  queryLedIndex = 0;
  lastTime = millis();
  setAllLEDs(false);
  Serial.print("Mode: ");
  Serial.println(blinkModeToString(currentMode));
}

void blinkLED(BlinkMode mode) {
  switch (mode) {
    case BlinkMode::OFF:
      setAllLEDs(false);
      break;

    case BlinkMode::ON:
      setAllLEDs(true);
      break;

    case BlinkMode::BLINK_SLOW:
    case BlinkMode::BLINK:
    case BlinkMode::BLINK_FAST: {
      unsigned long interval =
        (mode == BlinkMode::BLINK_SLOW) ? 1000 :
        (mode == BlinkMode::BLINK)      ? 250  : 50;

      if (millis() - lastBlinkTime >= interval) {
        ledState = !ledState;
        setAllLEDs(ledState);
        lastBlinkTime = millis();
      }
      break;
    }
    case BlinkMode::BLINK_BY_QUERY: {
      if (millis() - lastTime >= BLINK_INTERVAL) {
        // Turn off all, then light current one
        setAllLEDs(false);
        digitalWrite(QUERY_PINS[queryLedIndex], HIGH);
        queryLedIndex = (queryLedIndex + 1) % QUERY_COUNT;
        lastTime = millis();
      }
      break;
    }
    case BlinkMode::BLINK_RANDOM: {
      if (millis() - lastTime >= BLINK_INTERVAL) {
        setAllLEDs(false);
        digitalWrite(QUERY_PINS[random(QUERY_COUNT)], HIGH);
        lastTime = millis();
      }
      break;
    }
  }
}

void loop() {
  bool reading = digitalRead(BUTTON_PIN);

  // Reset debounce timer on any change
  if (reading != lastReading) {
    lastDebounceTime = millis();
    lastReading = reading;
  }

  // Only act after signal has been stable for DEBOUNCE_DELAY
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (lastReading != lastStableState) {
      lastStableState = lastReading;
      if (lastStableState == LOW) {  // falling edge = button pressed
        cycleMode();
      }
    }
  }

  // --- Boot button (GPIO 0) — previous mode ---
  bool readingBack = digitalRead(BOOT_BUTTON_PIN);

  if (readingBack != lastReadingBack) {
    lastDebounceTimeBack = millis();
    lastReadingBack = readingBack;
  }

  if ((millis() - lastDebounceTimeBack) > DEBOUNCE_DELAY) {
    if (lastReadingBack != lastStableStateBack) {
      lastStableStateBack = lastReadingBack;
      if (lastStableStateBack == LOW) {
        previousMode();
      }
    }
  }

  blinkLED(currentMode);
}