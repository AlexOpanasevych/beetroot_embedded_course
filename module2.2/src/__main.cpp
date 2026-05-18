// #include <Arduino.h>

// /*
//  * Wiring:
//  *   ESP32-S3              Relay module
//  *   GPIO_CTRL  ─────────► IN   (coil control)
//  *   GPIO_FB    ◄────────── COM─NO (normally-open contact, other end to GND)
//  *   3.3V / 5V  ─────────► VCC
//  *   GND        ─────────── GND
//  *
//  * GPIO_FB uses INPUT_PULLUP → reads LOW when NO contact closes.
//  *
//  * What is measured:
//  *   Activation delay  = time from "relay ON" command to contact closure (ISR).
//  *   After 10 valid measurements the average is printed.
//  */

// #define RELAY_CTRL_PIN  15   // OUTPUT – drives relay coil
// #define RELAY_FB_PIN    16   // INPUT  – reads NO contact state

// // ── ISR state ────────────────────────────────────────────────────────────────
// volatile bool     contactChanged = false;
// volatile uint32_t isrTimestamp   = 0;
// volatile bool     isClosed       = false;   // true when NO contact is closed

// void IRAM_ATTR onContactChange() {
//     uint32_t now = millis();
//     if (now - isrTimestamp < 10) return;    // simple 10-ms debounce
//     isrTimestamp   = now;
//     isClosed       = (digitalRead(RELAY_FB_PIN) == LOW);
//     contactChanged = true;
// }

// // ── Measurement config ────────────────────────────────────────────────────────
// const uint8_t  NUM_MEAS   = 10;
// const uint32_t TIMEOUT_MS = 1000;   // max wait for contact change
// const uint32_t SETTLE_MS  = 300;    // pause between measurements

// uint32_t activationDelay[NUM_MEAS];
// uint8_t  idx         = 0;
// uint32_t actionTime  = 0;
// uint32_t settleStart = 0;

// // ── State machine ─────────────────────────────────────────────────────────────
// enum State { SETTLE, RELAY_ON, WAIT_CLOSE, RELAY_OFF, WAIT_OPEN, DONE };
// State state = SETTLE;

// // ── Helpers ───────────────────────────────────────────────────────────────────
// void clearIsr() {
//     noInterrupts();
//     contactChanged = false;
//     interrupts();
// }

// void printResults() {
//     uint32_t sum = 0;
//     Serial.println();
//     Serial.println("==============================");
//     Serial.println("          RESULTS             ");
//     Serial.println("==============================");
//     Serial.println("  N  | Activation delay (ms)");
//     Serial.println("-----|----------------------");
//     for (uint8_t i = 0; i < NUM_MEAS; i++) {
//         Serial.printf(" %2d  |          %4lu\n", i + 1, activationDelay[i]);
//         sum += activationDelay[i];
//     }
//     Serial.println("-----|----------------------");
//     Serial.printf(" Avg |         %6.2f ms\n", (float)sum / NUM_MEAS);
//     Serial.println("==============================");
// }

// // ── Setup ─────────────────────────────────────────────────────────────────────
// void setup() {
//     Serial.begin(115200);
//     while (!Serial) delay(10);

//     pinMode(RELAY_CTRL_PIN, OUTPUT);
//     digitalWrite(RELAY_CTRL_PIN, LOW);

//     pinMode(RELAY_FB_PIN, INPUT_PULLUP);
//     attachInterrupt(digitalPinToInterrupt(RELAY_FB_PIN),
//                     onContactChange, CHANGE);

//     Serial.println("=== Relay Response Time Measurement ===");
//     Serial.printf("Performing %d measurements\n\n", NUM_MEAS);
//     settleStart = millis();
// }

// // ── Loop ──────────────────────────────────────────────────────────────────────
// void loop() {
//     switch (state) {

//         // ── Wait SETTLE_MS before each measurement ───────────────────────────
//         case SETTLE:
//             if (millis() - settleStart >= SETTLE_MS)
//                 state = RELAY_ON;
//             break;

//         // ── Activate relay, record timestamp ─────────────────────────────────
//         case RELAY_ON:
//             clearIsr();
//             actionTime = millis();
//             digitalWrite(RELAY_CTRL_PIN, HIGH);
//             Serial.printf("[%2d/%d] t=%lu ms | Relay ON\n",
//                           idx + 1, NUM_MEAS, actionTime);
//             state = WAIT_CLOSE;
//             break;

//         // ── Wait for contact to close (interrupt sets contactChanged) ─────────
//         case WAIT_CLOSE:
//             if (contactChanged && isClosed) {
//                 activationDelay[idx] = isrTimestamp - actionTime;
//                 Serial.printf("        contact CLOSED  +%lu ms  (ISR)\n",
//                               activationDelay[idx]);
//                 clearIsr();
//                 state = RELAY_OFF;
//             } else if (millis() - actionTime > TIMEOUT_MS) {
//                 Serial.println("        TIMEOUT — contact did not close, retrying\n");
//                 digitalWrite(RELAY_CTRL_PIN, LOW);
//                 clearIsr();
//                 settleStart = millis();
//                 state = SETTLE;     // retry, don't increment idx
//             }
//             break;

//         // ── Deactivate relay ──────────────────────────────────────────────────
//         case RELAY_OFF:
//             clearIsr();
//             actionTime = millis();
//             digitalWrite(RELAY_CTRL_PIN, LOW);
//             Serial.printf("        t=%lu ms | Relay OFF\n", actionTime);
//             state = WAIT_OPEN;
//             break;

//         // ── Wait for contact to open ──────────────────────────────────────────
//         case WAIT_OPEN: {
//             bool opened  = contactChanged && !isClosed;
//             bool timeout = (millis() - actionTime > TIMEOUT_MS);
//             if (opened || timeout) {
//                 if (opened)
//                     Serial.printf("        contact OPENED  +%lu ms  (ISR)\n\n",
//                                   isrTimestamp - actionTime);
//                 else
//                     Serial.println("        TIMEOUT — contact did not open\n");
//                 clearIsr();
//                 idx++;
//                 if (idx >= NUM_MEAS) state = DONE;
//                 else { settleStart = millis(); state = SETTLE; }
//             }
//             break;
//         }

//         // ── Print results and halt ────────────────────────────────────────────
//         case DONE:
//             printResults();
//             while (true) delay(60000);
//             break;
//     }
// }
