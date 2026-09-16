/*
 * Minimal DS1307 RTC driver on top of ESP-IDF's new I2C master driver.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
    bool                    present;
} ds1307_handle_t;

typedef struct {
    uint8_t year;   // 0 == 2000
    uint8_t month;  // 1-12
    uint8_t day;    // 1-31
    uint8_t hour;   // 0-23
    uint8_t minute;
    uint8_t second;
} ds1307_time_t;

// Probes the device and, if its oscillator is halted (factory default /
// after the backup battery was ever fully drained), clears the halt bit so
// the clock actually starts ticking. `out->present` reports whether the
// chip answered on the bus.
esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint8_t addr, ds1307_handle_t *out);

esp_err_t ds1307_read_time(ds1307_handle_t *h, ds1307_time_t *out);

// Unix seconds (UTC) for a DS1307 timestamp, computed with a
// timezone-independent calendar algorithm (no <time.h>/mktime involved).
uint32_t ds1307_to_unix(const ds1307_time_t *t);
