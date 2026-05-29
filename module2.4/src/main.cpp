/*
 * Module 2.4 — Button Debounce
 *
 * Board : ESP32-S3-DevKitC-1
 * Button: GPIO0 (BOOT button, active-LOW, board has 10k pull-up)
 *
 * Change TASK_NUM to select which task to run:
 *   1 — No debounce         (baseline, many false triggers)
 *   2 — Time-based debounce (ISR ignores edges < 50 ms apart)
 *   3 — State-based debounce(ISR queues event; task accepts only if still LOW)
 *   4 — Polling + FSM       (no ISR, 5 ms polling, state machine)
 *
 * Task 5 — Hardware RC filter:
 *   Add a 100 Ω series resistor between GPIO0 and the button,
 *   then a 100 nF capacitor from GPIO0 to GND.
 *   Keep the existing 10 kΩ pull-up.  The RC time constant (τ = 10 µs) low-
 *   pass filters glitches before they reach the pin.  Repeat tasks 1-4 and
 *   observe fewer false triggers even without software debounce.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Task 6 — Comparison table
 * ─────────────────────────────────────────────────────────────────────────────
 * Method          | False triggers | Latency  | Complexity | Notes
 * ────────────────┼────────────────┼──────────┼────────────┼──────────────────
 * No debounce     | High (5-20x)   | ~0 ms    | Very low   | Many counts/press
 * Time-based ISR  | Low            | ~0 ms    | Low        | May still fire on release
 * State-based ISR | Very low       | ~5 ms    | Medium     | 1 action per press
 * Polling FSM     | None           | ≤10 ms   | Medium     | Most stable
 * Hardware RC     | Low-medium     | ~0 ms    | Low (HW)   | Best combined w/ SW
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_1;
constexpr int DEBOUNCE_MS = 50;
constexpr const char *TAG = "DEBOUNCE";

// ── Select task ───────────────────────────────────────────────────
#define TASK_NUM 1
// ─────────────────────────────────────────────────────────────────


// ═════════════════════════════════════════════════════════════════
// Task 1: No debounce — baseline
//   Expected: 1 physical press → several interrupts logged
// ═════════════════════════════════════════════════════════════════
#if TASK_NUM == 1

static volatile int g_counter = 0;

static void IRAM_ATTR button_isr(void *arg)
{
    g_counter+=1;
}

extern "C" void app_main(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BUTTON_PIN);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_NEGEDGE;
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);

    ESP_LOGI(TAG, "Task 1: no debounce — press the button");

    int last = 0;
    while (1) {
        int cur = g_counter;
        if (cur != last) {
            last = cur;
            ESP_LOGI(TAG, "Interrupt #%d", last);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// ═════════════════════════════════════════════════════════════════
// Task 2: Time-based software debounce
//   Ignores interrupts that arrive < DEBOUNCE_MS after the previous.
//   Expected: fewer false triggers; release may still fire once.
// ═════════════════════════════════════════════════════════════════
#elif TASK_NUM == 2

static volatile int     g_counter    = 0;
static volatile int64_t g_last_isr_us = 0;

static void IRAM_ATTR button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if ((now - g_last_isr_us) >= (DEBOUNCE_MS * 1000LL)) {
        g_counter+=1;
        g_last_isr_us = now;
    }
}

extern "C" void app_main(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BUTTON_PIN);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_NEGEDGE;
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr, NULL);

    ESP_LOGI(TAG, "Task 2: time-based debounce (%d ms) — press the button",
             DEBOUNCE_MS);

    int last = 0;
    while (1) {
        int cur = g_counter;
        if (cur != last) {
            last = cur;
            ESP_LOGI(TAG, "Press #%d (time-based)", last);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// ═════════════════════════════════════════════════════════════════
// Task 3: State-based debounce
//   ISR only signals an event; a task accepts it only if the pin
//   is still LOW (button still held) after a brief settle delay.
//   Expected: exactly 1 action per press; release ignored.
// ═════════════════════════════════════════════════════════════════
#elif TASK_NUM == 3

static QueueHandle_t s_evt_queue;

static void IRAM_ATTR button_isr(void *arg)
{
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    xQueueSendFromISR(s_evt_queue, &pin, NULL);
}

static void button_task(void *arg)
{
    uint32_t pin;
    int counter = 0;
    while (1) {
        if (xQueueReceive(s_evt_queue, &pin, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(5));                   // let signal settle
            if (gpio_get_level((gpio_num_t)pin) == 0) {    // still pressed?
                counter++;
                ESP_LOGI(TAG, "Press #%d (state-based)", counter);
            }
        }
    }
}

extern "C" void app_main(void)
{
    s_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BUTTON_PIN);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_NEGEDGE;
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr,
                         (void *)(uintptr_t)BUTTON_PIN);

    xTaskCreate(button_task, "btn_task", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Task 3: state-based debounce — press the button");
}


// ═════════════════════════════════════════════════════════════════
// Task 4: Polling + state machine (no interrupts)
//   Button is sampled every 5 ms; a 3-state FSM filters glitches.
//   Expected: most stable; ≤10 ms reaction latency.
// ═════════════════════════════════════════════════════════════════
#elif TASK_NUM == 4

typedef enum { BTN_IDLE, BTN_DEBOUNCING, BTN_HELD } btn_state_t;

static void poll_task(void *arg)
{
    btn_state_t state        = BTN_IDLE;
    int64_t     debounce_start = 0;
    int         counter      = 0;

    while (1) {
        int     level = gpio_get_level(BUTTON_PIN);
        int64_t now   = esp_timer_get_time();

        switch (state) {
            case BTN_IDLE:
                if (level == 0) {
                    state          = BTN_DEBOUNCING;
                    debounce_start = now;
                }
                break;

            case BTN_DEBOUNCING:
                if (level == 1) {
                    state = BTN_IDLE;               // released before debounce — glitch
                } else if ((now - debounce_start) >= (DEBOUNCE_MS * 1000LL)) {
                    state = BTN_HELD;
                    counter++;
                    ESP_LOGI(TAG, "Press #%d (polling FSM)", counter);
                }
                break;

            case BTN_HELD:
                if (level == 1) {
                    state = BTN_IDLE;               // released
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));              // poll every 10 ms (1 tick minimum)
    }
}

extern "C" void app_main(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BUTTON_PIN);
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    xTaskCreate(poll_task, "poll_task", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Task 4: polling + FSM debounce — press the button");
}

#else
#error "Set TASK_NUM to 1, 2, 3, or 4"
#endif
