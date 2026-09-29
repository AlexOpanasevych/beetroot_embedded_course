#include "cpu_monitor.h"
#include "sensor_hub.h"
#include "pir_alarm.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

static const char *TAG = "CPU_MONITOR";

#define SAMPLE_PERIOD_MS  3000
#define CPU_LOAD_WARN_PCT 70.0f

static void cpu_monitor_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);

    uint32_t prev_idle_us = ulTaskGetIdleRunTimeCounter();
    uint32_t prev_wall_us = (uint32_t)esp_timer_get_time();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        uint32_t now_idle_us = ulTaskGetIdleRunTimeCounter();
        uint32_t now_wall_us = (uint32_t)esp_timer_get_time();

        // Unsigned wraparound makes this correct even across the ~4290s
        // overflow of the esp_timer-based run-time counter.
        uint32_t delta_idle_us = now_idle_us - prev_idle_us;
        uint32_t delta_wall_us = now_wall_us - prev_wall_us;
        prev_idle_us = now_idle_us;
        prev_wall_us = now_wall_us;

        // Total CPU capacity in this window is wall time * core count: every
        // core is running either a real task or its idle task at all times.
        uint32_t total_capacity_us = delta_wall_us * (uint32_t)configNUMBER_OF_CORES;

        float load_pct = 0.0f;
        if (total_capacity_us > 0) {
            load_pct = 100.0f * (1.0f - (float)delta_idle_us / (float)total_capacity_us);
            if (load_pct < 0.0f) load_pct = 0.0f;
            if (load_pct > 100.0f) load_pct = 100.0f;
        }

        sensor_hub_set_cpu_load(load_pct);

        if (load_pct >= CPU_LOAD_WARN_PCT) {
            ESP_LOGW(TAG, "CPU load %.1f%% (>= %.0f%% budget)", load_pct, CPU_LOAD_WARN_PCT);
        } else {
            ESP_LOGI(TAG, "CPU load %.1f%%", load_pct);
        }

        sensor_hub_log_and_reset_timing();
        pir_alarm_log_and_reset_isr_timing();

        esp_task_wdt_reset();
    }
}

void cpu_monitor_start(void)
{
    xTaskCreate(cpu_monitor_task, "CpuMonitor", 3072, NULL, 2, NULL);
}
