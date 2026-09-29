/*
 * sensor_hub — the single-consumer side of the multi-producer/single-consumer
 * queue pattern from module6.3, plus the mutex-protected shared snapshot
 * pattern from module6.5's report buffer.
 *
 * Producers (bme280_task, pir_alarm's motion task) only ever call
 * sensor_hub_post(); only aggregator_task (started by sensor_hub_init) ever
 * calls xQueueReceive() on the underlying queue, and it is the only writer
 * of the sensor/motion fields of system_state_t. Non-sensor status fields
 * (fan duty, Wi-Fi, CPU load) are written by their owners via the
 * sensor_hub_set_*() calls below, under the same mutex. Everyone reads it
 * through sensor_hub_get_state(), which takes a snapshot under that mutex.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SRC_BME280,
    SRC_MOTION,
} sensor_src_t;

typedef struct {
    sensor_src_t src;
    uint32_t timestamp_ms;

    // SRC_BME280
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    bool  bme280_ok;

    // SRC_MOTION
    bool motion_active;
} sensor_msg_t;

typedef struct {
    bool     bme280_ok;
    float    temperature_c;
    float    humidity_pct;
    float    pressure_hpa;
    uint32_t bme280_updated_ms;

    bool     motion_active;
    uint32_t motion_count;
    uint32_t last_motion_ms;

    uint32_t fan_duty;
    uint32_t fan_duty_max;

    bool wifi_connected;
    char ip_addr[16];

    float cpu_load_pct;
} system_state_t;

// Creates the queue/mutex and starts aggregator_task. Call once, before any
// producer task is started.
void sensor_hub_init(void);

// Producer-side API — safe to call from any task (not from ISR context; the
// PIR ISR itself only gives a semaphore, see pir_alarm.h).
void sensor_hub_post(const sensor_msg_t *msg);

// Consumer-side snapshot, safe to call from any task (dashboard, PID loop).
void sensor_hub_get_state(system_state_t *out);

// Called only from aggregator_task's own thread (fan_pid.cpp writes back the
// duty it just applied so the dashboard can show it).
void sensor_hub_set_fan_duty(uint32_t duty, uint32_t duty_max);

void sensor_hub_set_wifi(bool connected, const char *ip);

// Called from cpu_monitor.cpp's own task.
void sensor_hub_set_cpu_load(float load_pct);

// Logs this window's state-mutex hold-time stats (see crit_timing.h) and
// resets them for the next window. Called periodically from cpu_monitor.cpp.
void sensor_hub_log_and_reset_timing(void);
