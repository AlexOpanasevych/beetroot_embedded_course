#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define GPIO_A_PIN      GPIO_NUM_16
#define GPIO_B_PIN      GPIO_NUM_15
#define PCNT_HIGH_LIMIT  100
#define PCNT_LOW_LIMIT  -100

static const char *TAG = "pcnt";

static bool IRAM_ATTR on_reach(pcnt_unit_handle_t unit,
                               const pcnt_watch_event_data_t *edata,
                               void *user_ctx)
{
    BaseType_t woke = pdFALSE;
    QueueHandle_t q = static_cast<QueueHandle_t>(user_ctx);
    xQueueSendFromISR(q, &edata->watch_point_value, &woke);
    return woke == pdTRUE;
}

extern "C" void app_main()
{
    // --- unit ---
    pcnt_unit_config_t unit_cfg = {
        .low_limit  = PCNT_LOW_LIMIT,
        .high_limit = PCNT_HIGH_LIMIT,
    };
    pcnt_unit_handle_t unit = nullptr;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unit));

    // glitch filter: ignore pulses shorter than 1 µs
    pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(unit, &filter_cfg));

    // --- channels (x4 quadrature decoding) ---
    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num  = GPIO_A_PIN,
        .level_gpio_num = GPIO_B_PIN,
    };
    pcnt_channel_handle_t chan_a = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &chan_a_cfg, &chan_a));

    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num  = GPIO_B_PIN,
        .level_gpio_num = GPIO_A_PIN,
    };
    pcnt_channel_handle_t chan_b = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &chan_b_cfg, &chan_b));

    // channel A: count up on rising edge of A when B=low, down when B=high
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // channel B: mirrors A on B edges for x4 resolution
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // --- watch points: notify when limits are hit ---
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit, PCNT_HIGH_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit, PCNT_LOW_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(unit, 0));

    QueueHandle_t event_q = xQueueCreate(10, sizeof(int));
    pcnt_event_callbacks_t cbs = { .on_reach = on_reach };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(unit, &cbs, event_q));

    // --- start ---
    ESP_ERROR_CHECK(pcnt_unit_enable(unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
    ESP_ERROR_CHECK(pcnt_unit_start(unit));

    ESP_LOGI(TAG, "Encoder started on A=GPIO%d, B=GPIO%d", GPIO_A_PIN, GPIO_B_PIN);

    int count = 0;
    int watch_val = 0;
    while (true) {
        if (xQueueReceive(event_q, &watch_val, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG, "Watch point hit: %d", watch_val);
        }
        ESP_ERROR_CHECK(pcnt_unit_get_count(unit, &count));
        ESP_LOGI(TAG, "count = %d", count);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
