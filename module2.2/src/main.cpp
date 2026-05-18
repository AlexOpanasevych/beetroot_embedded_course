#include <Arduino.h>

/*
 * Wiring:
 *   Potentiometer:
 *     wiper → GPIO 10  (ADC)
 *     VCC   → 3.3V
 *     GND   → GND
 *
 *   SG90 servo:
 *     Brown  (GND)    → GND  (спільна земля з ESP32)
 *     Red    (VCC)    → 5V   (пін VBUS/5V на DevKitC-1, НЕ 3.3V)
 *     Orange (Signal) → GPIO 17
 *
 * ── Servo PWM protocol ───────────────────────────────────────────────────────
 * Frequency : 50 Hz  (period = 20 000 µs)
 * Pulse width → angle:
 *    500 µs  →   0°
 *   1500 µs  →  90°
 *   2400 µs  → 180°
 *
 *  ____
 * |    |_________________|    |___  ...  period = 20 000 µs
 *  ^pw^
 *  500..2400 µs
 */

// ── Піни ─────────────────────────────────────────────────────────────────────
#define POT_PIN    10   // Потенціометр → ADC1_CH9
#define SERVO_PIN  17   // Сигнальний дріт серво (жовтогарячий)

// ── Параметри серво SG90 ─────────────────────────────────────────────────────
const uint32_t PERIOD_US  = 20000;  // 20 мс → 50 Гц
const uint32_t PULSE_MIN  =   500;  // мкс →   0°
const uint32_t PULSE_MAX  =  2400;  // мкс → 180°

// Поточна ширина імпульсу (оновлюється з ADC)
volatile uint32_t pulseWidth = PULSE_MIN;

uint32_t periodStart = 0;

// ── Soft-PWM тік (викликати кожну ітерацію loop) ─────────────────────────────
void tickServo() {
    uint32_t elapsed = micros() - periodStart;

    if (elapsed >= PERIOD_US) {
        // Початок нового періоду — передній фронт імпульсу
        periodStart = micros();
        digitalWrite(SERVO_PIN, HIGH);
    } else if (elapsed >= pulseWidth) {
        // Задній фронт — решта періоду LOW
        digitalWrite(SERVO_PIN, LOW);
    }
}

// ── ADC (середнє з 8 зчитувань) ──────────────────────────────────────────────
uint16_t readADC() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(POT_PIN);
    return (uint16_t)(sum >> 3);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    analogReadResolution(12);        // 0–4095
    analogSetAttenuation(ADC_11db);  // 0–3.3 В

    pinMode(SERVO_PIN, OUTPUT);
    digitalWrite(SERVO_PIN, LOW);

    periodStart = micros();

    Serial.println("=== Servo SG90: керування потенціометром ===\n");
    Serial.println(" ADC(12b) | Pulse (µs) | Angle");
    Serial.println("----------+------------+------");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    // 1. Servo тік — кожна ітерація, критично для точності сигналу
    tickServo();

    // 2. Оновлення ширини імпульсу кожні 20 мс (один період)
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < 20) return;
    lastUpdate = millis();

    uint16_t adcVal = readADC();
    pulseWidth = (uint32_t)map(adcVal, 0, 4095, PULSE_MIN, PULSE_MAX);
    uint8_t angle  = (uint8_t)map(adcVal, 0, 4095, 0, 180);

    // 3. Serial Monitor кожні 200 мс
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 200) {
        lastPrint = millis();
        Serial.printf("  %4u    |    %4lu      | %3u°\n",
                      adcVal, pulseWidth, angle);
    }
}
