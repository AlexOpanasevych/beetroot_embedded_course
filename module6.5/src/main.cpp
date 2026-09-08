/*
 * Module 6.5 — Assembly line monitor: ISR part counter, dual-core report
 * buffer producers, and a sleeping highest-priority emergency-stop task.
 *
 *   - Part sensor  (ISR, GPIO4,  falling edge, debounced) : increments the
 *     global part counter. Simulates an optical sensor on the conveyor.
 *   - Counter/Rate task (Task 1, pinned Core 0, period 1 s) : reads the
 *     counter, derives parts/min, and (re)writes the shared report buffer.
 *   - Sensor task       (Task 2, pinned Core 1, period 1 s) : reads the ADC
 *     (noise / analog sensor) and appends its reading to the same buffer.
 *   - Emergency-stop task (Task 3, highest priority in the system) : blocks
 *     forever on a binary semaphore -> costs 0% CPU while idle. The
 *     emergency-button ISR gives the semaphore, waking it instantly.
 *   - Emergency button (ISR, GPIO0 / BOOT, falling edge, debounced) : gives
 *     the halt semaphore. Task 3 then lights the alarm LED, stamps
 *     "SYSTEM HALTED!" into the report buffer, and raises g_system_halted
 *     so the counter ISR and both report tasks stop touching shared state
 *     (the conveyor stays down until the board is reset).
 *
 * Shared-state protection:
 *   - g_parts_counter is written from ISR context (Core 0 or Core 1,
 *     whichever core takes the interrupt) and read from Task 1, so it is
 *     guarded by a portMUX_TYPE spinlock (safe for both ISR and task side,
 *     unlike a plain critical section which only masks one core's
 *     interrupts).
 *   - s_report_buffer is written by three different tasks running on both
 *     cores, so every access goes through a FreeRTOS mutex.
 *   - g_system_halted is set once by Task 3 and only ever read afterwards,
 *     so a plain volatile flag is enough.
 *
 * Board: ESP32-S3-DevKitC-1
 * Part sensor button -> GPIO4  (active-LOW, internal pull-up)
 * Emergency button    -> GPIO0 (BOOT, active-LOW, board pull-up)
 * Alarm LED            -> GPIO5  (active-HIGH, LED + resistor -> GND)
 * Line sensor (ADC)    -> GPIO10 (ADC1_CH9, noise or real analog sensor)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "LINE_MONITOR";

// ── Hardware ─────────────────────────────────────────────────────────────
#define PART_SENSOR_PIN     GPIO_NUM_4
#define EMERGENCY_BTN_PIN   GPIO_NUM_0
#define ALARM_LED_PIN       GPIO_NUM_5

#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_9   // GPIO10
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define ADC_BITWIDTH        ADC_BITWIDTH_12

#define DEBOUNCE_TICKS      pdMS_TO_TICKS(150)
#define REPORT_PERIOD_MS    1000
#define REPORT_BUF_LEN      256

// ── Shared state ─────────────────────────────────────────────────────────
static volatile uint32_t g_parts_counter = 0;
static volatile bool     g_system_halted = false;

static portMUX_TYPE       s_counter_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t  s_report_mutex;
static SemaphoreHandle_t  s_halt_sem;

static char s_report_buffer[REPORT_BUF_LEN] = "System starting...";

static adc_oneshot_unit_handle_t s_adc;

// ── ISR #1: part sensor -> counts produced parts ────────────────────────
static void IRAM_ATTR part_sensor_isr_handler(void *arg)
{
    static uint32_t last_tick = 0;
    uint32_t now = xTaskGetTickCountFromISR();

    if (g_system_halted) {
        return; // conveyor is down, stop counting new parts
    }
    if (now - last_tick < DEBOUNCE_TICKS) {
        return;
    }
    last_tick = now;

    portENTER_CRITICAL_ISR(&s_counter_mux);
    g_parts_counter++;
    portEXIT_CRITICAL_ISR(&s_counter_mux);
}

// ── ISR #2: emergency button -> wakes the sleeping stop task ────────────
static void IRAM_ATTR emergency_button_isr_handler(void *arg)
{
    static uint32_t last_tick = 0;
    uint32_t now = xTaskGetTickCountFromISR();

    if (now - last_tick < DEBOUNCE_TICKS) {
        return;
    }
    last_tick = now;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_halt_sem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ── Task 1 (Core 0): counter -> parts/min, (re)writes the report buffer ──
static void task_counter_report(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Counter/rate task started", xPortGetCoreID());
    uint32_t last_count = 0;

    while (1) {
        if (!g_system_halted) {
            portENTER_CRITICAL(&s_counter_mux);
            uint32_t current = g_parts_counter;
            portEXIT_CRITICAL(&s_counter_mux);

            uint32_t parts_per_sec = current - last_count;
            last_count = current;
            uint32_t parts_per_min = parts_per_sec * 60;

            if (xSemaphoreTake(s_report_mutex, portMAX_DELAY) == pdTRUE) {
                snprintf(s_report_buffer, REPORT_BUF_LEN,
                         "[Report] Parts=%lu  Rate=%lu pcs/min",
                         (unsigned long)current, (unsigned long)parts_per_min);
                xSemaphoreGive(s_report_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    }
}

// ── Task 2 (Core 1): reads the ADC, appends its reading to the buffer ───
static void task_sensor_report(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] ADC sensor task started", xPortGetCoreID());

    while (1) {
        if (!g_system_halted) {
            int raw = 0;
            if (adc_oneshot_read(s_adc, ADC_CHANNEL, &raw) == ESP_OK) {
                char sensor_part[64];
                snprintf(sensor_part, sizeof(sensor_part), "  |  ADC=%d raw", raw);

                if (xSemaphoreTake(s_report_mutex, portMAX_DELAY) == pdTRUE) {
                    strncat(s_report_buffer, sensor_part,
                            REPORT_BUF_LEN - strlen(s_report_buffer) - 1);
                    ESP_LOGI(TAG, "%s", s_report_buffer);
                    xSemaphoreGive(s_report_mutex);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
    }
}

// ── Task 3: sleeps on a semaphore, highest priority in the system ───────
static void task_emergency_stop(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Emergency-stop task ready, sleeping", xPortGetCoreID());

    while (1) {
        // Blocks forever -> 0% CPU until the ISR gives the semaphore.
        if (xSemaphoreTake(s_halt_sem, portMAX_DELAY) == pdTRUE) {
            gpio_set_level(ALARM_LED_PIN, 1);
            g_system_halted = true; // stops the counter ISR and both report tasks

            if (xSemaphoreTake(s_report_mutex, portMAX_DELAY) == pdTRUE) {
                uint32_t final_count;
                portENTER_CRITICAL(&s_counter_mux);
                final_count = g_parts_counter;
                portEXIT_CRITICAL(&s_counter_mux);

                snprintf(s_report_buffer, REPORT_BUF_LEN,
                         "SYSTEM HALTED! Total parts produced: %lu",
                         (unsigned long)final_count);
                ESP_LOGE(TAG, "%s", s_report_buffer);
                xSemaphoreGive(s_report_mutex);
            }
        }
    }
}

// ── Init helpers ──────────────────────────────────────────────────────────
static void gpio_init_all(void)
{
    gpio_config_t part_sensor_conf = {
        .pin_bit_mask = (1ULL << PART_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&part_sensor_conf);

    gpio_config_t emergency_conf = {
        .pin_bit_mask = (1ULL << EMERGENCY_BTN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&emergency_conf);

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << ALARM_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(ALARM_LED_PIN, 0);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PART_SENSOR_PIN, part_sensor_isr_handler, NULL);
    gpio_isr_handler_add(EMERGENCY_BTN_PIN, emergency_button_isr_handler, NULL);
}

static void adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL, &chan_cfg));
}

extern "C" void app_main(void)
{
    gpio_init_all();
    adc_init();

    s_report_mutex = xSemaphoreCreateMutex();
    s_halt_sem = xSemaphoreCreateBinary();
    if (s_report_mutex == NULL || s_halt_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        abort();
    }

    xTaskCreatePinnedToCore(task_counter_report, "CounterReport", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_sensor_report, "SensorReport", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_emergency_stop, "EmergencyStop", 4096, NULL,
                             configMAX_PRIORITIES - 1, NULL, tskNO_AFFINITY);

    ESP_LOGI(TAG, "System started: part-counter ISR + 2 report tasks (core0/core1) "
                  "+ sleeping emergency-stop task (priority %d)", configMAX_PRIORITIES - 1);
}
