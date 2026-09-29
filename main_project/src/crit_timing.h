/*
 * Lightweight critical-section timing instrumentation (min/max/avg
 * microseconds, via esp_timer_get_time()). Used to actually measure — not
 * just assume from reading the code — how long the project's two real
 * critical sections stay held: the sensor_hub state mutex (sensor_hub.cpp)
 * and the PIR GPIO ISR (pir_alarm.cpp).
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t count;
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;
} crit_timing_stats_t;

void crit_timing_reset(crit_timing_stats_t *stats);

// IRAM-resident (IRAM_ATTR on the definition), so callable from an IRAM_ATTR
// ISR. Pure integer math with no locking of its own: the caller serializes
// access to `stats` (sensor_hub holds its state mutex, pir_alarm a spinlock
// shared with its ISR).
void crit_timing_record(crit_timing_stats_t *stats, uint32_t elapsed_us);

// Not ISR-safe (uses ESP_LOGI). One line: "<label>: n=.. min=..us max=..us avg=..us".
void crit_timing_log(const char *tag, const char *label, const crit_timing_stats_t *stats);
