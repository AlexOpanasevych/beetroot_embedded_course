#include "pir_alarm.h"
#include "sensor_hub.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "PIR_ALARM";

// Desk Guardian PCB pins (miniproject_module5), unchanged from that board.
#define PIR_PIN   GPIO_NUM_4  // PIR_IN, RC-filtered on the board, R5 already pulls it down
#define BUZZER_PIN GPIO_NUM_5
#define LED1_PIN  GPIO_NUM_6
#define LED2_PIN  GPIO_NUM_7

// Same flash/gap cadence constants as module1.3's blink pattern, reused here
// for the buzzer/LED "bark" instead of a plain LED flash.
#define ALERT_FLASH_MS 80
#define ALERT_GAP_MS   60
#define ALERT_PULSES   4

static SemaphoreHandle_t s_motion_sem;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_motion_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void bark(void)
{
    for (int i = 0; i < ALERT_PULSES; i++) {
        gpio_set_level(BUZZER_PIN, 1);
        gpio_set_level(LED1_PIN, 1);
        gpio_set_level(LED2_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(ALERT_FLASH_MS));

        gpio_set_level(BUZZER_PIN, 0);
        gpio_set_level(LED1_PIN, 0);
        gpio_set_level(LED2_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(ALERT_GAP_MS));
    }
    // eyes stay lit a bit longer than the bark so the alert is visible even
    // after the buzzer stops
    gpio_set_level(LED1_PIN, 1);
    gpio_set_level(LED2_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);
}

static void pir_task(void *pvParameters)
{
    ESP_LOGI(TAG, "PIR task ready, sleeping at 0%% CPU until motion");

    while (1) {
        // Blocks forever -> costs nothing until the ISR gives the semaphore.
        if (xSemaphoreTake(s_motion_sem, portMAX_DELAY) == pdTRUE) {
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            ESP_LOGW(TAG, "Motion detected at t=%lu ms", (unsigned long)now_ms);

            sensor_msg_t msg = {};
            msg.src = SRC_MOTION;
            msg.timestamp_ms = now_ms;
            msg.motion_active = true;
            sensor_hub_post(&msg);

            bark();

            sensor_msg_t cleared = {};
            cleared.src = SRC_MOTION;
            cleared.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            cleared.motion_active = false;
            sensor_hub_post(&cleared);

            // Ignore retriggers while the PIR is still settling / someone is
            // still standing there — a single pending semaphore give during
            // this window (if any) will fire the next iteration immediately.
            vTaskDelay(pdMS_TO_TICKS(CONFIG_MOTION_COOLDOWN_MS));
        }
    }
}

void pir_alarm_init(void)
{
    gpio_config_t pir_conf = {
        .pin_bit_mask = (1ULL << PIR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // R5 on the board already pulls this down
        .intr_type = GPIO_INTR_POSEDGE,        // PIR output goes HIGH on motion
    };
    gpio_config(&pir_conf);

    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN) | (1ULL << LED1_PIN) | (1ULL << LED2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);
    gpio_set_level(BUZZER_PIN, 0);
    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED2_PIN, 0);

    s_motion_sem = xSemaphoreCreateBinary();
    if (s_motion_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create motion semaphore");
        abort();
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIR_PIN, pir_isr_handler, NULL);

    xTaskCreate(pir_task, "PirTask", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
}
