/*
 * Module 6.3 — Safe multi-sensor -> single-consumer data transfer via xQueue
 *
 * Three independent producers write into ONE shared FreeRTOS queue, and a
 * single consumer task is the only reader:
 *
 *   - LDR producer     (task, core 0) : ADC1 CH8 / GPIO9,  period 200 ms
 *   - Potentiometer producer (task, core 1) : ADC2 CH0 / GPIO11, period 150 ms
 *   - BOOT button producer (ISR, GPIO0, falling edge, debounced)
 *
 * Because every producer only ever calls xQueueSend()/xQueueSendFromISR()
 * and only the consumer calls xQueueReceive(), there is no shared mutable
 * state between tasks (unlike the classic shared_counter race-condition
 * demo) — the queue itself provides the mutual exclusion, and FreeRTOS
 * guarantees each queued item is delivered whole, in order, to a single
 * receiver. The ISR producer additionally has to use the *FromISR variant
 * and portYIELD_FROM_ISR() to wake the consumer immediately, since it may
 * not block or call the non-ISR queue API from interrupt context.
 *
 * Board: ESP32-S3-DevKitC-1
 * LDR          -> GPIO9  (ADC1_CH8, VCC -> LDR -> GPIO9 -> 10k -> GND)
 * Potentiometer-> GPIO11 (ADC2_CH0, wiper)
 * BOOT button  -> GPIO0  (active-LOW, board pull-up)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "SENSOR_QUEUE";

// ── Hardware ─────────────────────────────────────────────────────────────
#define LDR_ADC_UNIT        ADC_UNIT_1
#define LDR_ADC_CHANNEL     ADC_CHANNEL_8   // GPIO9
#define POT_ADC_UNIT        ADC_UNIT_2
#define POT_ADC_CHANNEL     ADC_CHANNEL_0   // GPIO11
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define ADC_BITWIDTH        ADC_BITWIDTH_12

#define BOOT_BUTTON_PIN     GPIO_NUM_0
#define DEBOUNCE_TICKS      pdMS_TO_TICKS(200)

#define LDR_PERIOD_MS       200
#define POT_PERIOD_MS       150
#define QUEUE_LENGTH        20

// ── Shared message type ─────────────────────────────────────────────────
typedef enum {
    SENSOR_LDR,
    SENSOR_POT,
    SENSOR_BUTTON,
} sensor_id_t;

typedef struct {
    sensor_id_t id;
    int32_t     value;         // raw ADC reading, or 1 for a button press
    uint32_t    timestamp_ms;
} sensor_msg_t;

static QueueHandle_t xSensorQueue;
static adc_oneshot_unit_handle_t s_adc1;   // LDR
static adc_oneshot_unit_handle_t s_adc2;   // potentiometer

// ── BOOT button ISR (producer #3) ───────────────────────────────────────
static void IRAM_ATTR boot_button_isr_handler(void *arg)
{
    static uint32_t last_press_tick = 0;
    uint32_t now = xTaskGetTickCountFromISR();

    if (now - last_press_tick < DEBOUNCE_TICKS) {
        return;
    }
    last_press_tick = now;

    sensor_msg_t msg = {
        .id = SENSOR_BUTTON,
        .value = 1,
        .timestamp_ms = (uint32_t)(now * portTICK_PERIOD_MS),
    };

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(xSensorQueue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ── LDR producer (task) ──────────────────────────────────────────────────
static void task_ldr_producer(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] LDR producer started", xPortGetCoreID());

    while (1) {
        int raw = 0;
        if (adc_oneshot_read(s_adc1, LDR_ADC_CHANNEL, &raw) == ESP_OK) {
            sensor_msg_t msg = {
                .id = SENSOR_LDR,
                .value = raw,
                .timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            };
            xQueueSend(xSensorQueue, &msg, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(LDR_PERIOD_MS));
    }
}

// ── Potentiometer producer (task) ────────────────────────────────────────
static void task_pot_producer(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Potentiometer producer started", xPortGetCoreID());

    while (1) {
        int raw = 0;
        if (adc_oneshot_read(s_adc2, POT_ADC_CHANNEL, &raw) == ESP_OK) {
            sensor_msg_t msg = {
                .id = SENSOR_POT,
                .value = raw,
                .timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            };
            xQueueSend(xSensorQueue, &msg, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(POT_PERIOD_MS));
    }
}

// ── Consumer (the only task that ever reads the queue) ──────────────────
static void task_consumer(void *pvParameters)
{
    ESP_LOGI(TAG, "[Core %d] Consumer started, waiting for sensor data...", xPortGetCoreID());

    uint32_t ldr_count = 0, pot_count = 0, button_count = 0;
    sensor_msg_t msg;

    while (1) {
        xQueueReceive(xSensorQueue, &msg, portMAX_DELAY);

        switch (msg.id) {
            case SENSOR_LDR:
                ldr_count++;
                ESP_LOGI(TAG, "[t=%lu ms] LDR    raw=%4ld  (total=%lu)",
                         (unsigned long)msg.timestamp_ms, (long)msg.value, (unsigned long)ldr_count);
                break;

            case SENSOR_POT:
                pot_count++;
                ESP_LOGI(TAG, "[t=%lu ms] POT    raw=%4ld  (total=%lu)",
                         (unsigned long)msg.timestamp_ms, (long)msg.value, (unsigned long)pot_count);
                break;

            case SENSOR_BUTTON:
                button_count++;
                ESP_LOGW(TAG, "[t=%lu ms] BUTTON pressed        (total=%lu)",
                         (unsigned long)msg.timestamp_ms, (unsigned long)button_count);
                break;
        }
    }
}

// ── Init helpers ──────────────────────────────────────────────────────────
static void adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit1_cfg = { .unit_id = LDR_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit1_cfg, &s_adc1));
    const adc_oneshot_chan_cfg_t ldr_chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, LDR_ADC_CHANNEL, &ldr_chan_cfg));

    const adc_oneshot_unit_init_cfg_t unit2_cfg = { .unit_id = POT_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit2_cfg, &s_adc2));
    const adc_oneshot_chan_cfg_t pot_chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc2, POT_ADC_CHANNEL, &pot_chan_cfg));
}

static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_BUTTON_PIN, boot_button_isr_handler, NULL);
}

extern "C" void app_main(void)
{
    adc_init();
    button_init();

    xSensorQueue = xQueueCreate(QUEUE_LENGTH, sizeof(sensor_msg_t));
    if (xSensorQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor queue");
        abort();
    }

    // Consumer gets the highest priority so it drains the queue promptly
    // and never becomes the bottleneck for the producers.
    xTaskCreatePinnedToCore(task_consumer, "Consumer", 4096, NULL, 10, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(task_ldr_producer, "LdrProducer", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_pot_producer, "PotProducer", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "System started: 2 periodic sensor tasks + 1 button ISR -> 1 queue -> 1 consumer");
}
