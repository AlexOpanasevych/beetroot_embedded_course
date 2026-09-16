/*
 * Minimal Bosch BME280 driver (temperature / humidity / pressure) on top of
 * ESP-IDF's new I2C master driver. Self-contained on purpose: the compiled
 * calibration + compensation math follows the reference formulas from the
 * BME280 datasheet directly, rather than pulling in an external component.
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

typedef struct {
    i2c_master_dev_handle_t dev;
    bme280_calib_t          calib;
    bool                    present; // true once init() has verified the chip ID
} bme280_handle_t;

typedef struct {
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
} bme280_data_t;

// Probes the chip ID register and loads calibration data. `out` is filled
// in either way; `out->present` says whether the device actually answered.
esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint8_t addr, bme280_handle_t *out);

// Triggers one forced-mode measurement, waits for it to complete, and
// returns the compensated readings. Fails fast with ESP_ERR_INVALID_STATE
// if init() never found the chip.
esp_err_t bme280_read(bme280_handle_t *h, bme280_data_t *out);
