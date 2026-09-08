/*
 * Module 6.1 — ДЗ з уроку 2.3, переписане на RTOS-задачах
 *
 * У модулі 2.3 три світлодіоди блимали з різними інтервалами в одному
 * циклі app_main() через кооперативний non-blocking Led::update()
 * (кожен світлодіод сам стежив за millis() і перемикався, коли підходив
 * його час). Тут та сама логіка (три світлодіоди, три незалежні
 * інтервали) реалізована через справжні RTOS-задачі: кожен світлодіод —
 * окрема задача FreeRTOS, створена через xTaskCreate, яка блокується на
 * vTaskDelay() між перемиканнями. Планувальник FreeRTOS сам перемикає
 * задачі, тож циклу з ручним опитуванням більше не потрібно.
 *
 * Board: ESP32-S3-DevKitC-1
 * LED1 -> GPIO4  (200 мс)
 * LED2 -> GPIO5  (500 мс)
 * LED3 -> GPIO6  (1000 мс)
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct LedTaskParams {
    gpio_num_t pin;
    uint32_t   interval_ms;
    const char *name;
};

static LedTaskParams led1_params = { GPIO_NUM_4, 200,  "led1" };
static LedTaskParams led2_params = { GPIO_NUM_5, 500,  "led2" };
static LedTaskParams led3_params = { GPIO_NUM_6, 1000, "led3" };

static void led_blink_task(void *pvParameters) {
    const LedTaskParams *params = static_cast<const LedTaskParams *>(pvParameters);

    gpio_reset_pin(params->pin);
    gpio_set_direction(params->pin, GPIO_MODE_OUTPUT);
    gpio_set_level(params->pin, 0);

    int state = 0;
    const TickType_t delay = pdMS_TO_TICKS(params->interval_ms);

    while (1) {
        state ^= 1;
        gpio_set_level(params->pin, state);
        vTaskDelay(delay);
    }
}

extern "C" void app_main() {
    xTaskCreate(led_blink_task, led1_params.name, 2048, &led1_params, 5, NULL);
    xTaskCreate(led_blink_task, led2_params.name, 2048, &led2_params, 5, NULL);
    xTaskCreate(led_blink_task, led3_params.name, 2048, &led3_params, 5, NULL);
}
