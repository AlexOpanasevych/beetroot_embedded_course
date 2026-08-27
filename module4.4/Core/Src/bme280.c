/**
  ******************************************************************************
  * @file    bme280.c
  * @brief   Minimal polling I2C driver for the Bosch BME280.
  *          Register map and the 32-bit integer compensation formulas are
  *          taken from the Bosch BME280 datasheet section 4 ("Data readout")
  *          and section 8.2 ("Compensation formulas along with an accuracy
  *          note"), rewritten as plain functions instead of the vendor's
  *          global-state reference driver.
  ******************************************************************************
  */
#include "bme280.h"

#define REG_CHIP_ID       0xD0U
#define REG_CALIB_00      0x88U /* dig_T1..dig_P9, 26 bytes */
#define REG_CALIB_H1      0xA1U /* dig_H1, 1 byte */
#define REG_CALIB_H2      0xE1U /* dig_H2..dig_H6, 7 bytes */
#define REG_CTRL_HUM      0xF2U
#define REG_CTRL_MEAS     0xF4U
#define REG_CONFIG        0xF5U
#define REG_PRESS_MSB     0xF7U /* press(3) + temp(3) + hum(2) = 8 bytes, burst read */

#define CHIP_ID_EXPECTED  0x60U
#define I2C_TIMEOUT_MS    100U

/* oversampling x1 for all three, forced mode (sensor sleeps between reads -> lower self-heating) */
#define CTRL_HUM_OSRS_H1    0x01U
#define CTRL_MEAS_OSRS_T1_P1_FORCED  0x25U /* osrs_t=001, osrs_p=001, mode=01 (forced) */

static bool i2c_read(BME280_Handle *dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(dev->hi2c, BME280_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             buf, len, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool i2c_write_reg(BME280_Handle *dev, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(dev->hi2c, BME280_I2C_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                              &value, 1, I2C_TIMEOUT_MS) == HAL_OK;
}

static bool load_calibration(BME280_Handle *dev)
{
    uint8_t c0[26];
    uint8_t h1;
    uint8_t c1[7];

    if (!i2c_read(dev, REG_CALIB_00, c0, sizeof(c0))) return false;
    if (!i2c_read(dev, REG_CALIB_H1, &h1, 1)) return false;
    if (!i2c_read(dev, REG_CALIB_H2, c1, sizeof(c1))) return false;

    dev->dig_T1 = (uint16_t)(c0[0] | (c0[1] << 8));
    dev->dig_T2 = (int16_t)(c0[2] | (c0[3] << 8));
    dev->dig_T3 = (int16_t)(c0[4] | (c0[5] << 8));

    dev->dig_P1 = (uint16_t)(c0[6] | (c0[7] << 8));
    dev->dig_P2 = (int16_t)(c0[8] | (c0[9] << 8));
    dev->dig_P3 = (int16_t)(c0[10] | (c0[11] << 8));
    dev->dig_P4 = (int16_t)(c0[12] | (c0[13] << 8));
    dev->dig_P5 = (int16_t)(c0[14] | (c0[15] << 8));
    dev->dig_P6 = (int16_t)(c0[16] | (c0[17] << 8));
    dev->dig_P7 = (int16_t)(c0[18] | (c0[19] << 8));
    dev->dig_P8 = (int16_t)(c0[20] | (c0[21] << 8));
    dev->dig_P9 = (int16_t)(c0[22] | (c0[23] << 8));

    dev->dig_H1 = h1;
    dev->dig_H2 = (int16_t)(c1[0] | (c1[1] << 8));
    dev->dig_H3 = c1[2];
    /* dig_H4/H5 are packed as two overlapping 12-bit signed fields sharing byte c1[4] */
    dev->dig_H4 = (int16_t)((int8_t)c1[3] * 16 + (c1[4] & 0x0FU));
    dev->dig_H5 = (int16_t)((int8_t)c1[5] * 16 + (c1[4] >> 4));
    dev->dig_H6 = (int8_t)c1[6];

    return true;
}

bool BME280_Init(BME280_Handle *dev, I2C_HandleTypeDef *hi2c)
{
    uint8_t chip_id = 0;

    dev->hi2c = hi2c;
    dev->t_fine = 0;

    if (!i2c_read(dev, REG_CHIP_ID, &chip_id, 1) || chip_id != CHIP_ID_EXPECTED) {
        return false;
    }
    if (!load_calibration(dev)) {
        return false;
    }
    /* humidity oversampling must be written before ctrl_meas for it to take effect */
    if (!i2c_write_reg(dev, REG_CTRL_HUM, CTRL_HUM_OSRS_H1)) return false;
    if (!i2c_write_reg(dev, REG_CONFIG, 0x00U)) return false; /* no IIR filter, no standby (forced mode ignores standby anyway) */

    return true;
}

/* Datasheet 8.2, integer path: returns temperature in 0.01 degC steps and sets dev->t_fine. */
static int32_t compensate_temperature(BME280_Handle *dev, int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dev->dig_T1 << 1))) * (int32_t)dev->dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)dev->dig_T1) * ((adc_T >> 4) - (int32_t)dev->dig_T1)) >> 12) *
                    (int32_t)dev->dig_T3) >> 14;
    dev->t_fine = var1 + var2;
    return (dev->t_fine * 5 + 128) >> 8;
}

/* Datasheet 8.2, 64-bit integer path: returns pressure in Q24.8 fixed point (Pa). */
static uint32_t compensate_pressure(BME280_Handle *dev, int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = (int64_t)dev->t_fine - 128000;
    var2 = var1 * var1 * (int64_t)dev->dig_P6;
    var2 = var2 + ((var1 * (int64_t)dev->dig_P5) << 17);
    var2 = var2 + (((int64_t)dev->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->dig_P3) >> 8) + ((var1 * (int64_t)dev->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * (int64_t)dev->dig_P1 >> 33;

    if (var1 == 0) {
        return 0; /* avoid divide-by-zero */
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)dev->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)dev->dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)dev->dig_P7 << 4);

    return (uint32_t)p;
}

/* Datasheet 8.2, integer path: returns humidity in Q22.10 fixed point (%RH). */
static uint32_t compensate_humidity(BME280_Handle *dev, int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = dev->t_fine - 76800;
    v_x1_u32r = ((((adc_H << 14) - (((int32_t)dev->dig_H4) << 20) - (((int32_t)dev->dig_H5) * v_x1_u32r)) +
                  16384) >> 15) *
                (((((((v_x1_u32r * (int32_t)dev->dig_H6) >> 10) *
                     (((v_x1_u32r * (int32_t)dev->dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
                  (int32_t)dev->dig_H2 + 8192) >> 14);
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (int32_t)dev->dig_H1) >> 4);
    v_x1_u32r = v_x1_u32r < 0 ? 0 : v_x1_u32r;
    v_x1_u32r = v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r;

    return (uint32_t)(v_x1_u32r >> 12);
}

bool BME280_Read(BME280_Handle *dev, float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    uint8_t raw[8];

    /* Kick off one forced-mode conversion; the sensor auto-returns to sleep when done. */
    if (!i2c_write_reg(dev, REG_CTRL_MEAS, CTRL_MEAS_OSRS_T1_P1_FORCED)) {
        return false;
    }
    HAL_Delay(10); /* worst-case conversion time at x1/x1/x1 oversampling is well under this */

    if (!i2c_read(dev, REG_PRESS_MSB, raw, sizeof(raw))) {
        return false;
    }

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];

    int32_t temp_c_x100 = compensate_temperature(dev, adc_T); /* must run first: fills t_fine */
    uint32_t press_q24_8 = compensate_pressure(dev, adc_P);
    uint32_t hum_q22_10 = compensate_humidity(dev, adc_H);

    *temperature_c = (float)temp_c_x100 / 100.0f;
    *pressure_hpa = ((float)press_q24_8 / 256.0f) / 100.0f; /* Pa (Q24.8) -> hPa */
    *humidity_pct = (float)hum_q22_10 / 1024.0f;

    return true;
}
