#include "sensor_hub.h"
#include "fan_pid.h"
#include "crit_timing.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "SENSOR_HUB";

#define QUEUE_LENGTH 20

static QueueHandle_t     s_queue;
static SemaphoreHandle_t s_state_mutex;
static system_state_t    s_state = {};

// How long s_state_mutex is actually held (not counting wait-to-acquire
// time), sampled at every call site and reported by cpu_monitor.cpp.
// Only ever read or written while holding s_state_mutex.
static crit_timing_stats_t s_mutex_stats;

void sensor_hub_post(const sensor_msg_t *msg)
{
    xQueueSend(s_queue, msg, portMAX_DELAY);
}

void sensor_hub_get_state(system_state_t *out)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        int64_t t0 = esp_timer_get_time();
        *out = s_state;
        crit_timing_record(&s_mutex_stats, (uint32_t)(esp_timer_get_time() - t0));
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_set_fan_duty(uint32_t duty, uint32_t duty_max)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        int64_t t0 = esp_timer_get_time();
        s_state.fan_duty = duty;
        s_state.fan_duty_max = duty_max;
        crit_timing_record(&s_mutex_stats, (uint32_t)(esp_timer_get_time() - t0));
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_set_wifi(bool connected, const char *ip)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        int64_t t0 = esp_timer_get_time();
        s_state.wifi_connected = connected;
        if (ip != NULL) {
            strncpy(s_state.ip_addr, ip, sizeof(s_state.ip_addr) - 1);
            s_state.ip_addr[sizeof(s_state.ip_addr) - 1] = '\0';
        }
        crit_timing_record(&s_mutex_stats, (uint32_t)(esp_timer_get_time() - t0));
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_set_cpu_load(float load_pct)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        int64_t t0 = esp_timer_get_time();
        s_state.cpu_load_pct = load_pct;
        crit_timing_record(&s_mutex_stats, (uint32_t)(esp_timer_get_time() - t0));
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_log_and_reset_timing(void)
{
    crit_timing_stats_t snapshot;
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    snapshot = s_mutex_stats;
    crit_timing_reset(&s_mutex_stats);
    xSemaphoreGive(s_state_mutex);

    crit_timing_log(TAG, "state mutex hold time", &snapshot);
}

// ── Single consumer of s_queue (module6.3 pattern) ──────────────────────────
static void aggregator_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Aggregator started, waiting for sensor events...");
    esp_task_wdt_add(NULL);
    sensor_msg_t msg;

    while (1) {
        // bme280_task posts at least every BME280_POLL_MS (2s), well inside
        // the task WDT timeout, so a bounded wait is enough to feed the
        // watchdog without changing the steady-state behavior.
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
            esp_task_wdt_reset();
            continue;
        }

        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        int64_t crit_t0 = esp_timer_get_time();

        switch (msg.src) {
            case SRC_BME280:
                s_state.bme280_ok = msg.bme280_ok;
                if (msg.bme280_ok) {
                    s_state.temperature_c = msg.temperature_c;
                    s_state.humidity_pct = msg.humidity_pct;
                    s_state.pressure_hpa = msg.pressure_hpa;
                    s_state.bme280_updated_ms = msg.timestamp_ms;
                }
                break;

            case SRC_MOTION:
                s_state.motion_active = msg.motion_active;
                if (msg.motion_active) {
                    s_state.motion_count++;
                    s_state.last_motion_ms = msg.timestamp_ms;
                }
                break;
        }

        crit_timing_record(&s_mutex_stats, (uint32_t)(esp_timer_get_time() - crit_t0));
        xSemaphoreGive(s_state_mutex);

        // PID recompute is driven by fresh temperature data rather than a
        // fixed timer — dt is measured inside fan_pid, so this is just as
        // valid a control loop as module5.6's fixed 50 ms one, just paced
        // to how often the BME280 actually has something new to say.
        if (msg.src == SRC_BME280 && msg.bme280_ok) {
            fan_pid_on_new_temperature(msg.temperature_c);
        }

        esp_task_wdt_reset();
    }
}

void sensor_hub_init(void)
{
    s_queue = xQueueCreate(QUEUE_LENGTH, sizeof(sensor_msg_t));
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create queue/mutex");
        abort();
    }
    crit_timing_reset(&s_mutex_stats);

    xTaskCreate(aggregator_task, "Aggregator", 4096, NULL, 10, NULL);
}
