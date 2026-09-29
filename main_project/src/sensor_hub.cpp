#include "sensor_hub.h"
#include "fan_pid.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "SENSOR_HUB";

#define QUEUE_LENGTH 20

static QueueHandle_t     s_queue;
static SemaphoreHandle_t s_state_mutex;
static system_state_t    s_state = {};

void sensor_hub_post(const sensor_msg_t *msg)
{
    xQueueSend(s_queue, msg, portMAX_DELAY);
}

void sensor_hub_get_state(system_state_t *out)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        *out = s_state;
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_set_fan_duty(uint32_t duty, uint32_t duty_max)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.fan_duty = duty;
        s_state.fan_duty_max = duty_max;
        xSemaphoreGive(s_state_mutex);
    }
}

void sensor_hub_set_wifi(bool connected, const char *ip)
{
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
        s_state.wifi_connected = connected;
        if (ip != NULL) {
            strncpy(s_state.ip_addr, ip, sizeof(s_state.ip_addr) - 1);
            s_state.ip_addr[sizeof(s_state.ip_addr) - 1] = '\0';
        }
        xSemaphoreGive(s_state_mutex);
    }
}

// ── Single consumer of s_queue (module6.3 pattern) ──────────────────────────
static void aggregator_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Aggregator started, waiting for sensor events...");
    sensor_msg_t msg;

    while (1) {
        xQueueReceive(s_queue, &msg, portMAX_DELAY);

        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }

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

        xSemaphoreGive(s_state_mutex);

        // PID recompute is driven by fresh temperature data rather than a
        // fixed timer — dt is measured inside fan_pid, so this is just as
        // valid a control loop as module5.6's fixed 50 ms one, just paced
        // to how often the BME280 actually has something new to say.
        if (msg.src == SRC_BME280 && msg.bme280_ok) {
            fan_pid_on_new_temperature(msg.temperature_c);
        }
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

    xTaskCreate(aggregator_task, "Aggregator", 4096, NULL, 10, NULL);
}
