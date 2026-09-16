/*
 * Manager Task — the system's orchestrator and the only task that ever
 * blocks in vTaskDelay(). Every CYCLE_PERIOD_MS it:
 *
 *   1. drains any UART commands that arrived since the last cycle,
 *   2. wakes the Sensor task and waits for a fresh reading,
 *   3. hands that reading to the Display task,
 *   4. every LOG_INTERVAL_MINUTES, hands it to the Flash Writer task too,
 *   5. goes back to sleep for the rest of the cycle.
 *
 * Every other task blocks on a queue/notification with 0% CPU, so almost
 * all of that "sleep for the rest of the cycle" time is spent in FreeRTOS'
 * automatic tickless-idle light sleep (see power_management_init() in
 * main.cpp) rather than busy-waiting.
 */

#include "common.h"
#include "flash_log.h"
#include <stdio.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "MANAGER";

static bool s_logging_enabled = true;
static uint32_t s_seconds_since_log = LOG_INTERVAL_MINUTES * 60; // log on the first cycle

static void handle_uart_command(uart_cmd_t cmd)
{
    switch (cmd) {
        case CMD_STATUS: {
            printf("\r\n--- STATUS ---\r\n");
            printf("Logging      : %s\r\n", s_logging_enabled ? "ON" : "OFF");
            printf("Flash        : %s, %lu entries written, %u%% used\r\n",
                   flashlog_is_full() ? "FULL" : "OK",
                   (unsigned long)flashlog_entries_written(), flashlog_usage_pct());
            printf("Log interval : %d min\r\n", LOG_INTERVAL_MINUTES);
            printf("--------------\r\n");
            break;
        }
        case CMD_LOG_ON:
            s_logging_enabled = true;
            printf("\r\n[MGR] Logging enabled.\r\n");
            break;
        case CMD_LOG_OFF:
            s_logging_enabled = false;
            printf("\r\n[MGR] Logging disabled.\r\n");
            break;
        case CMD_FLUSH: {
            flash_msg_t m = { .op = FLASH_OP_FLUSH };
            xQueueSend(xFlashQueue, &m, portMAX_DELAY);
            break;
        }
        case CMD_ERASE: {
            flash_msg_t m = { .op = FLASH_OP_ERASE };
            xQueueSend(xFlashQueue, &m, portMAX_DELAY);
            s_seconds_since_log = LOG_INTERVAL_MINUTES * 60;
            break;
        }
        default:
            break;
    }
}

void manager_task(void *pv)
{
    printf("\r\n[MGR] System ready. Commands: status | log on | log off | flush | erase\r\n");

    while (1) {
        uart_cmd_t cmd;
        while (xQueueReceive(xUartCmdQueue, &cmd, 0) == pdTRUE) {
            handle_uart_command(cmd);
        }

        xTaskNotifyGive(xSensorTaskHandle);

        sensor_sample_t sample;
        if (xQueueReceive(xSensorQueue, &sample, pdMS_TO_TICKS(500)) == pdTRUE) {
            display_msg_t dmsg = {
                .sample = sample,
                .logging_enabled = s_logging_enabled,
                .flash_full = flashlog_is_full(),
                .flash_pct = flashlog_usage_pct(),
            };
            xQueueOverwrite(xDisplayQueue, &dmsg);

            s_seconds_since_log += CYCLE_PERIOD_MS / 1000;
            if (s_logging_enabled && !flashlog_is_full() && sample.valid &&
                s_seconds_since_log >= (uint32_t)(LOG_INTERVAL_MINUTES * 60)) {
                s_seconds_since_log = 0;

                log_entry_t entry = {
                    .timestamp = sample.unix_time,
                    .temp_c_x100 = (int16_t)lroundf(sample.temperature_c * 100.0f),
                    .humidity_pct_x100 = (uint16_t)lroundf(sample.humidity_pct * 100.0f),
                    .pressure_mmhg = (uint16_t)lroundf(sample.pressure_hpa * 0.750062f),
                };
                flash_msg_t fmsg = { .op = FLASH_OP_APPEND, .entry = entry };
                xQueueSend(xFlashQueue, &fmsg, portMAX_DELAY);
            }
        } else {
            ESP_LOGW(TAG, "Sensor read timed out");
        }

        vTaskDelay(pdMS_TO_TICKS(CYCLE_PERIOD_MS));
    }
}
