/*
 * Periodic CPU load sampling via FreeRTOS run-time stats
 * (configGENERATE_RUN_TIME_STATS, see sdkconfig.defaults). Reports load as a
 * rolling window (delta between samples), not a lifetime average, so it
 * actually reflects current conditions. Result is logged and pushed into
 * sensor_hub for the dashboard.
 */
#pragma once

void cpu_monitor_start(void);
