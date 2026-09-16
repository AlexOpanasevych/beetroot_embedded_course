#include "ds1307.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "DS1307";

#define REG_SECONDS 0x00 // bit7 = CH (clock halt)

static uint8_t bcd2dec(uint8_t bcd) { return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F)); }

static esp_err_t read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint8_t addr, ds1307_handle_t *out)
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

    uint8_t seconds_reg = 0;
    err = read_regs(out->dev, REG_SECONDS, &seconds_reg, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chip not found: %s", esp_err_to_name(err));
        return err;
    }

    if (seconds_reg & 0x80) {
        ESP_LOGW(TAG, "oscillator was halted (CH bit set) - starting it now; "
                      "the clock will run from 2000-01-01 00:00:00 unless the "
                      "time has been set externally");
        write_reg(out->dev, REG_SECONDS, seconds_reg & 0x7F);
    }

    out->present = true;
    ESP_LOGI(TAG, "found at 0x%02X", addr);
    return ESP_OK;
}

esp_err_t ds1307_read_time(ds1307_handle_t *h, ds1307_time_t *out)
{
    if (!h->present) return ESP_ERR_INVALID_STATE;

    uint8_t raw[7];
    esp_err_t err = read_regs(h->dev, REG_SECONDS, raw, sizeof(raw));
    if (err != ESP_OK) return err;

    out->second = bcd2dec(raw[0] & 0x7F);
    out->minute = bcd2dec(raw[1] & 0x7F);
    out->hour   = bcd2dec(raw[2] & 0x3F); // 24-hour mode assumed (bit6 clear)
    // raw[3] is day-of-week, unused
    out->day    = bcd2dec(raw[4] & 0x3F);
    out->month  = bcd2dec(raw[5] & 0x1F);
    out->year   = bcd2dec(raw[6]);

    return ESP_OK;
}

// Howard Hinnant's civil_from_days algorithm, days since 1970-01-01 (UTC),
// for a proleptic Gregorian calendar. Avoids any <time.h>/timezone
// dependency for a deterministic unix-seconds conversion.
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                 // [0, 399]
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;      // [0, 146096]
    return era * 146097 + (int64_t)doe - 719468;
}

uint32_t ds1307_to_unix(const ds1307_time_t *t)
{
    int64_t days = days_from_civil(2000 + t->year, t->month, t->day);
    int64_t seconds = days * 86400 + t->hour * 3600 + t->minute * 60 + t->second;
    return (uint32_t)seconds;
}
