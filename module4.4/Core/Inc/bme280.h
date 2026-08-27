/**
  ******************************************************************************
  * @file    bme280.h
  * @brief   Minimal polling I2C driver for the Bosch BME280 (temperature,
  *          humidity, pressure). Compensation formulas follow the Bosch
  *          BME280 datasheet (32-bit integer variant).
  ******************************************************************************
  */
#ifndef __BME280_H
#define __BME280_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* SDO tied to GND -> 0x76, SDO tied to VDDIO -> 0x77 */
#define BME280_I2C_ADDR   (0x76U << 1)

typedef struct {
    I2C_HandleTypeDef *hi2c;

    /* Calibration data read once at Init from the sensor's NVM registers */
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;

    int32_t t_fine; /* shared fine-temperature term used by pressure/humidity compensation */
} BME280_Handle;

/* Returns true if the chip-id register read back the expected 0x60 and calibration load succeeded. */
bool BME280_Init(BME280_Handle *dev, I2C_HandleTypeDef *hi2c);

/* Triggers one forced-mode measurement and reads it back.
 * temperature_c: degrees Celsius, humidity_pct: %RH, pressure_hpa: hectopascals.
 * Returns true on a successful I2C transaction. */
bool BME280_Read(BME280_Handle *dev, float *temperature_c, float *humidity_pct, float *pressure_hpa);

#ifdef __cplusplus
}
#endif

#endif /* __BME280_H */
