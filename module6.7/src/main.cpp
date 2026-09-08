/*
 * Module 6.7 — refactor of a deliberately broken ADC -> JSON pipeline
 * (original: 6.7hometask.c).
 *
 * Bugs fixed:
 *   1. tx_buffer was malloc'd but only free()'d on the success branch ->
 *      leaked on every simulated Wi-Fi failure. Replaced with std::string
 *      so the buffer is released automatically on every path (RAII).
 *   2. json_packet[8192] lived on a task created with only 2048 bytes of
 *      stack -> guaranteed overflow. The payload is a few dozen bytes; a
 *      128-byte buffer plus a properly sized task stack fixes it.
 *   3. sprintf() had no bound -> switched to snprintf() with a truncation
 *      check.
 *   4. "%d" was used to print a size_t -> "%zu".
 *   5. The periodic callback was named *_isr_callback and marked
 *      IRAM_ATTR, but esp_timer callbacks run in the timer service task
 *      (not real interrupt context) unless ISR dispatch is explicitly
 *      requested, so that was misleading and IRAM_ATTR was pointless.
 *      Renamed, dropped IRAM_ATTR, and trimmed it to just read + enqueue
 *      so it can't stall other timers; all logging moved to the consumer
 *      task.
 *   6. No error checking anywhere (queue/ADC/timer/task creation) ->
 *      wrapped in ESP_ERROR_CHECK / explicit checks.
 *
 * Board: ESP32-S3-DevKitC-1, analog input on GPIO1 (ADC1_CH0).
 */

#include <cstdio>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_adc/adc_oneshot.h"

namespace {

constexpr const char *TAG = "ADC_JSON_SENDER";

constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;
constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_0; // GPIO1
constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t ADC_BITWIDTH = ADC_BITWIDTH_DEFAULT;

constexpr uint64_t SAMPLE_PERIOD_US = 100000; // 100 ms
constexpr UBaseType_t QUEUE_LENGTH = 10;
constexpr uint32_t PROCESSOR_TASK_STACK = 4096;
constexpr UBaseType_t PROCESSOR_TASK_PRIORITY = 5;
constexpr int SEND_SUCCESS_THRESHOLD = 3; // rand() % 10 > 3 -> ~60% success

QueueHandle_t s_adc_queue = nullptr;
adc_oneshot_unit_handle_t s_adc_handle = nullptr;

// Runs in the esp_timer service task, not real interrupt context, so it's
// safe to call adc_oneshot_read() here. Kept minimal (read + enqueue only)
// so it never holds up any other timer callback.
void sample_timer_callback(void *arg) {
    int raw_adc_value = 0;
    if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL, &raw_adc_value) != ESP_OK) {
        return;
    }

    if (xQueueSend(s_adc_queue, &raw_adc_value, 0) != pdTRUE) {
        ESP_LOGW(TAG, "ADC queue full, sample dropped");
    }
}

void data_processor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Processor task started");

    while (true) {
        int adc_val = 0;
        if (xQueueReceive(s_adc_queue, &adc_val, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        char json_packet[128];
        int written = snprintf(json_packet, sizeof(json_packet),
                                "{\"sensor_id\": 42, \"adc_value\": %d, \"status\": \"OK\"}",
                                adc_val);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(json_packet)) {
            ESP_LOGE(TAG, "JSON packet truncated, dropping sample");
            continue;
        }

        // std::string owns the buffer, so it's released on every path
        // below - nothing left to leak.
        std::string tx_buffer(json_packet);

        bool send_success = (esp_random() % 10) > SEND_SUCCESS_THRESHOLD;

        if (send_success) {
            ESP_LOGI(TAG, "Data sent via Wi-Fi: %zu bytes", tx_buffer.size());
        } else {
            ESP_LOGW(TAG, "Wi-Fi error! Retrying in next loop...");
        }
    }
}

void init_adc() {
    const adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    const adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL, &chan_config));
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Booting ADC -> JSON sender");

    s_adc_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    if (s_adc_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create ADC queue");
        return;
    }

    init_adc();

    if (xTaskCreate(data_processor_task, "Processor", PROCESSOR_TASK_STACK,
                     nullptr, PROCESSOR_TASK_PRIORITY, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create processor task");
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &sample_timer_callback,
        .name = "adc_sample_timer",
    };
    esp_timer_handle_t sample_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sample_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sample_timer, SAMPLE_PERIOD_US));
}
