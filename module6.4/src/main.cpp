/*
 * Module 6.4 — Electronic safe: password FSM (Consumer) driven by an
 * ideal button-event FSM (Producer), connected through a FreeRTOS queue.
 *
 * Password: Short - Short - Long.
 * Any wrong event, or more than 3 seconds of silence while a password
 * is partially entered, resets the consumer FSM back to its initial state.
 *
 * Board: ESP32-S3-DevKitC-1, BOOT button -> GPIO0 (active-LOW, board pull-up)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define INPUT_TIMEOUT_MS 3000

static const char *TAG = "SAFE_FSM";

// ==========================================
// 1. АБСТРАКЦІЇ (Словник нашої системи)
// ==========================================

// Стани Головного Автомата (Сейф)
typedef enum {
    SAFE_STATE_WAIT_S1,   // чекаємо перше коротке
    SAFE_STATE_WAIT_S2,   // "Коротке" прийнято, чекаємо друге коротке
    SAFE_STATE_WAIT_LONG, // "Коротке-Коротке" прийнято, чекаємо довге
} safe_state_t;

// Події, які генерує Кнопка
typedef enum {
    EVENT_NONE,
    EVENT_SHORT_PRESS,
    EVENT_LONG_PRESS
} system_event_t;

// Стани Автомата Кнопки (Антибрязкіт + Таймінг)
typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_DEBOUNCE,
    BTN_STATE_PRESSED,
    BTN_STATE_WAIT_RELEASE
} btn_state_t;

QueueHandle_t xEventQueue;

// ==========================================
// 2. АВТОМАТ КНОПКИ (Producer)
// Працює кожні 10 мс, не боїться жодних шумів і ЕМП
// ==========================================
void task_button_fsm(void *pvParameters) {
    btn_state_t btn_state = BTN_STATE_IDLE;
    uint32_t hold_time = 0;
    system_event_t event_to_send;

    while (1) {
        int level = gpio_get_level(BOOT_BUTTON_PIN);

        switch (btn_state) {

            case BTN_STATE_IDLE:
                switch (level) {
                    case 0: // Побачили нуль - починаємо перевірку
                        btn_state = BTN_STATE_DEBOUNCE;
                        hold_time = 0;
                        break;
                    default: break;
                }
                break;

            case BTN_STATE_DEBOUNCE:
                hold_time += 10;
                switch (hold_time) {
                    case 50: // Пройшло 50 мс
                        switch (level) {
                            case 0: // Кнопка все ще натиснута! Це не шум.
                                btn_state = BTN_STATE_PRESSED;
                                hold_time = 0;
                                break;
                            case 1: // Це був просто шум (відскок)
                                btn_state = BTN_STATE_IDLE;
                                break;
                        }
                        break;
                    default: break;
                }
                break;

            case BTN_STATE_PRESSED:
                hold_time += 10;
                switch (level) {
                    case 1: // Відпустили раніше, ніж за 1000 мс -> Коротке натискання
                        event_to_send = EVENT_SHORT_PRESS;
                        xQueueSend(xEventQueue, &event_to_send, 0);
                        btn_state = BTN_STATE_IDLE;
                        break;

                    case 0: // Все ще тримають...
                        switch (hold_time) {
                            case 1000: // Досягли 1 секунди! -> Довге натискання
                                event_to_send = EVENT_LONG_PRESS;
                                xQueueSend(xEventQueue, &event_to_send, 0);
                                btn_state = BTN_STATE_WAIT_RELEASE;
                                break;
                            default: break;
                        }
                        break;
                }
                break;

            case BTN_STATE_WAIT_RELEASE:
                switch (level) {
                    case 1:
                        btn_state = BTN_STATE_IDLE;
                        break;
                    default: break;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==========================================
// 3. АВТОМАТ СЕЙФА (Consumer)
// ==========================================

// Скидає автомат у початковий стан з поясненням причини в лог.
static safe_state_t safe_reset(const char *reason) {
    ESP_LOGW(TAG, "Скидання пароля: %s. Стан: WAIT_S1.", reason);
    return SAFE_STATE_WAIT_S1;
}

void task_safe_fsm(void *pvParameters) {
    safe_state_t current_state = SAFE_STATE_WAIT_S1;
    system_event_t current_event;

    ESP_LOGI(TAG, "Сейф готовий до вводу пароля. Стан: WAIT_S1.");

    while (1) {
        // Поки очікуємо перше натискання, немає що таймаутити — чекаємо
        // безкінечно. Після першого натискання будь-яка тиша довша за
        // INPUT_TIMEOUT_MS означає "затупив" і має скинути автомат.
        TickType_t wait_ticks = (current_state == SAFE_STATE_WAIT_S1)
                                     ? portMAX_DELAY
                                     : pdMS_TO_TICKS(INPUT_TIMEOUT_MS);

        BaseType_t got_event = xQueueReceive(xEventQueue, &current_event, wait_ticks);

        if (got_event == pdFALSE) {
            // Таймаут — користувач "затупив" більше 3 секунд.
            current_state = safe_reset("тайм-аут вводу");
            continue;
        }

        switch (current_state) {

            case SAFE_STATE_WAIT_S1:
                switch (current_event) {
                    case EVENT_SHORT_PRESS:
                        ESP_LOGI(TAG, "Крок 1/3: Коротке. Стан: WAIT_S2.");
                        current_state = SAFE_STATE_WAIT_S2;
                        break;
                    case EVENT_LONG_PRESS:
                        current_state = safe_reset("очікувалось Коротке, отримано Довге");
                        break;
                    default: break;
                }
                break;

            case SAFE_STATE_WAIT_S2:
                switch (current_event) {
                    case EVENT_SHORT_PRESS:
                        ESP_LOGI(TAG, "Крок 2/3: Коротке-Коротке. Стан: WAIT_LONG.");
                        current_state = SAFE_STATE_WAIT_LONG;
                        break;
                    case EVENT_LONG_PRESS:
                        current_state = safe_reset("очікувалось Коротке, отримано Довге");
                        break;
                    default: break;
                }
                break;

            case SAFE_STATE_WAIT_LONG:
                switch (current_event) {
                    case EVENT_LONG_PRESS:
                        ESP_LOGE(TAG, "СЕЙФ ВІДКРИТО! Пароль вірний.");
                        current_state = safe_reset("успішне відкриття, готові до нової спроби");
                        break;
                    case EVENT_SHORT_PRESS:
                        current_state = safe_reset("очікувалось Довге, отримано Коротке");
                        break;
                    default: break;
                }
                break;
        }
    }
}

// ==========================================
// 4. ІНІЦІАЛІЗАЦІЯ
// ==========================================
extern "C" void app_main(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    xEventQueue = xQueueCreate(10, sizeof(system_event_t));
    if (xEventQueue == NULL) abort();

    xTaskCreate(task_button_fsm, "Btn_FSM", 2048, NULL, 5, NULL);
    xTaskCreate(task_safe_fsm, "Safe_FSM", 4096, NULL, 5, NULL);
}
