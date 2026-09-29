#include "pir_alarm.h"
#include "sensor_hub.h"
#include "crit_timing.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

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

// Time actually spent inside the ISR body. esp_timer_get_time() and
// crit_timing_record() are both IRAM-resident. The spinlock serializes the
// ISR's update against the task-side snapshot/reset (the ISR may run on the
// other core, so a plain interrupt mask wouldn't be enough).
static crit_timing_stats_t s_isr_stats;
static portMUX_TYPE s_isr_stats_lock = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    int64_t t0 = esp_timer_get_time();

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_motion_sem, &xHigherPriorityTaskWoken);

    uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - t0);
    portENTER_CRITICAL_ISR(&s_isr_stats_lock);
    crit_timing_record(&s_isr_stats, elapsed_us);
    portEXIT_CRITICAL_ISR(&s_isr_stats_lock);

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
    ESP_LOGI(TAG, "PIR task ready, sleeping near 0%% CPU until motion");
    esp_task_wdt_add(NULL);

    while (1) {
        // Bounded wait instead of portMAX_DELAY: the ISR still wakes this up
        // immediately on real motion, but a 1s poll lets the task feed the
        // watchdog while idle (which can otherwise be hours between events).
        if (xSemaphoreTake(s_motion_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
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
            // The cooldown is a menuconfig value and bark + cooldown already
            // exceeds the 5s task WDT at the default, so sleep in slices.
            for (int remaining = CONFIG_MOTION_COOLDOWN_MS; remaining > 0; remaining -= 1000) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(remaining < 1000 ? remaining : 1000));
            }
        }

        esp_task_wdt_reset();
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
    crit_timing_reset(&s_isr_stats);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIR_PIN, pir_isr_handler, NULL);

    xTaskCreate(pir_task, "PirTask", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
}

void pir_alarm_log_and_reset_isr_timing(void)
{
    crit_timing_stats_t snapshot;
    portENTER_CRITICAL(&s_isr_stats_lock);
    snapshot = s_isr_stats;
    crit_timing_reset(&s_isr_stats);
    portEXIT_CRITICAL(&s_isr_stats_lock);

    crit_timing_log(TAG, "PIR ISR duration", &snapshot);
}
