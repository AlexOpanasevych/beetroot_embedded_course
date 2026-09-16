#include "bme280.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BME280";

#define REG_CHIP_ID     0xD0
#define REG_CALIB_00    0x88 // 0x88..0x9F, 24 bytes (T1..P9)
#define REG_CALIB_A1    0xA1 // dig_H1
#define REG_CALIB_E1    0xE1 // 0xE1..0xE7, 7 bytes (H2..H6)
#define REG_CTRL_HUM    0xF2
#define REG_STATUS      0xF3
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_DATA        0xF7 // 0xF7..0xFE, 8 bytes: press(3) temp(3) hum(2)

#define CHIP_ID_BME280  0x60

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

static void load_calibration(bme280_calib_t *c, const uint8_t *b00_9f, uint8_t h1, const uint8_t *e1_e7)
{
    c->dig_T1 = (uint16_t)(b00_9f[1] << 8 | b00_9f[0]);
    c->dig_T2 = (int16_t)(b00_9f[3] << 8 | b00_9f[2]);
    c->dig_T3 = (int16_t)(b00_9f[5] << 8 | b00_9f[4]);
    c->dig_P1 = (uint16_t)(b00_9f[7] << 8 | b00_9f[6]);
    c->dig_P2 = (int16_t)(b00_9f[9] << 8 | b00_9f[8]);
    c->dig_P3 = (int16_t)(b00_9f[11] << 8 | b00_9f[10]);
    c->dig_P4 = (int16_t)(b00_9f[13] << 8 | b00_9f[12]);
    c->dig_P5 = (int16_t)(b00_9f[15] << 8 | b00_9f[14]);
    c->dig_P6 = (int16_t)(b00_9f[17] << 8 | b00_9f[16]);
    c->dig_P7 = (int16_t)(b00_9f[19] << 8 | b00_9f[18]);
    c->dig_P8 = (int16_t)(b00_9f[21] << 8 | b00_9f[20]);
    c->dig_P9 = (int16_t)(b00_9f[23] << 8 | b00_9f[22]);

    c->dig_H1 = h1;
    c->dig_H2 = (int16_t)(e1_e7[1] << 8 | e1_e7[0]);
    c->dig_H3 = e1_e7[2];
    c->dig_H4 = (int16_t)((int8_t)e1_e7[3] << 4 | (e1_e7[4] & 0x0F));
    c->dig_H5 = (int16_t)((int8_t)e1_e7[5] << 4 | (e1_e7[4] >> 4));
    c->dig_H6 = (int8_t)e1_e7[6];
}

esp_err_t bme280_init(i2c_master_bus_handle_t bus, uint8_t addr, bme280_handle_t *out)
{
    memset(out, 0, sizeof(*out));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &out->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t chip_id = 0;
    err = read_regs(out->dev, REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK || chip_id != CHIP_ID_BME280) {
        ESP_LOGE(TAG, "chip not found (err=%s, id=0x%02X)", esp_err_to_name(err), chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t b00_9f[24];
    uint8_t h1;
    uint8_t e1_e7[7];
    err = read_regs(out->dev, REG_CALIB_00, b00_9f, sizeof(b00_9f));
    if (err != ESP_OK) return err;
    err = read_regs(out->dev, REG_CALIB_A1, &h1, 1);
    if (err != ESP_OK) return err;
    err = read_regs(out->dev, REG_CALIB_E1, e1_e7, sizeof(e1_e7));
    if (err != ESP_OK) return err;

    load_calibration(&out->calib, b00_9f, h1, e1_e7);

    // config: standby time doesn't matter (we drive forced-mode reads),
    // IIR filter off for a simple, lag-free reading.
    write_reg(out->dev, REG_CONFIG, 0x00);

    out->present = true;
    ESP_LOGI(TAG, "found at 0x%02X", addr);
    return ESP_OK;
}

esp_err_t bme280_read(bme280_handle_t *h, bme280_data_t *out)
{
    if (!h->present) return ESP_ERR_INVALID_STATE;

    // Humidity oversampling must be written before ctrl_meas for it to
    // take effect; oversampling x1 on all three channels is plenty for a
    // once-a-second weather log.
    esp_err_t err = write_reg(h->dev, REG_CTRL_HUM, 0x01);
    if (err != ESP_OK) return err;

    // mode=forced(01), osrs_t=001, osrs_p=001
    err = write_reg(h->dev, REG_CTRL_MEAS, 0x25);
    if (err != ESP_OK) return err;

    // Forced-mode conversion at x1 oversampling takes ~9.3 ms; poll the
    // "measuring" bit with a margin instead of hardcoding a delay.
    for (int i = 0; i < 20; i++) {
        uint8_t status = 0;
        if (read_regs(h->dev, REG_STATUS, &status, 1) == ESP_OK && (status & 0x08) == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    uint8_t raw[8];
    err = read_regs(h->dev, REG_DATA, raw, sizeof(raw));
    if (err != ESP_OK) return err;

    int32_t adc_P = (int32_t)raw[0] << 12 | (int32_t)raw[1] << 4 | (raw[2] >> 4);
    int32_t adc_T = (int32_t)raw[3] << 12 | (int32_t)raw[4] << 4 | (raw[5] >> 4);
    int32_t adc_H = (int32_t)raw[6] << 8 | raw[7];

    const bme280_calib_t *c = &h->calib;
    int32_t t_fine;

    double var1 = (((double)adc_T) / 16384.0 - ((double)c->dig_T1) / 1024.0) * ((double)c->dig_T2);
    double var2 = ((((double)adc_T) / 131072.0 - ((double)c->dig_T1) / 8192.0) *
                   (((double)adc_T) / 131072.0 - ((double)c->dig_T1) / 8192.0)) * ((double)c->dig_T3);
    t_fine = (int32_t)(var1 + var2);
    out->temperature_c = (float)((var1 + var2) / 5120.0);

    var1 = ((double)t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)c->dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)c->dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)c->dig_P4) * 65536.0);
    var1 = (((double)c->dig_P3) * var1 * var1 / 524288.0 + ((double)c->dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)c->dig_P1);
    if (var1 == 0.0) {
        out->pressure_hpa = 0.0f;
    } else {
        double p = 1048576.0 - (double)adc_P;
        p = (p - (var2 / 4096.0)) * 6250.0 / var1;
        var1 = ((double)c->dig_P9) * p * p / 2147483648.0;
        var2 = p * ((double)c->dig_P8) / 32768.0;
        p = p + (var1 + var2 + ((double)c->dig_P7)) / 16.0;
        out->pressure_hpa = (float)(p / 100.0); // Pa -> hPa
    }

    double var_h = (((double)t_fine) - 76800.0);
    var_h = (adc_H - (((double)c->dig_H4) * 64.0 + ((double)c->dig_H5) / 16384.0 * var_h)) *
            (((double)c->dig_H2) / 65536.0 * (1.0 + ((double)c->dig_H6) / 67108864.0 * var_h *
            (1.0 + ((double)c->dig_H3) / 67108864.0 * var_h)));
    var_h = var_h * (1.0 - ((double)c->dig_H1) * var_h / 524288.0);
    if (var_h > 100.0) var_h = 100.0;
    else if (var_h < 0.0) var_h = 0.0;
    out->humidity_pct = (float)var_h;

    return ESP_OK;
}
