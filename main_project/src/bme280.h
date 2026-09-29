/*
 * Minimal polling I2C driver for the Bosch BME280 (temperature, humidity,
 * pressure), ported from module4.4's STM32/HAL version to ESP-IDF's
 * driver/i2c_master.h. Register map and compensation formulas are unchanged
 * (Bosch BME280 datasheet section 4 "Data readout" and section 8.2
 * "Compensation formulas", 32-bit integer variant).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

// SDO tied to GND -> 0x76, SDO tied to VDDIO -> 0x77
#define BME280_I2C_ADDR 0x76U

typedef struct {
    i2c_master_dev_handle_t dev;

    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;

    int32_t t_fine;
} bme280_handle_t;

// Adds the BME280 as a device on an already-initialized i2c_master bus,
// verifies the chip ID and loads calibration data.
bool bme280_init(bme280_handle_t *dev, i2c_master_bus_handle_t bus);

// Triggers one forced-mode measurement and reads it back.
bool bme280_read(bme280_handle_t *dev, float *temperature_c, float *humidity_pct, float *pressure_hpa);
