#include "common.h"
#include "bme280.h"
#include "ds1307.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "SENSOR";

static bme280_handle_t s_bme;
static ds1307_handle_t s_rtc;

esp_err_t sensor_task_init(i2c_master_bus_handle_t bus)
{
    esp_err_t err = bme280_init(bus, BME280_I2C_ADDR, &s_bme);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BME280 init failed: %s (readings will report invalid until fixed)", esp_err_to_name(err));
    }
    err = ds1307_init(bus, DS1307_I2C_ADDR, &s_rtc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS1307 init failed: %s (readings will report invalid until fixed)", esp_err_to_name(err));
    }
    return ESP_OK; // a missing/failed sensor is reported, not fatal
}

// Waits for the Manager task's per-cycle trigger, reads both I2C devices
// under xI2CMutex, and hands the combined sample back over xSensorQueue.
void sensor_task(void *pv)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensor_sample_t sample = {};

        if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            bme280_data_t bme_data;
            esp_err_t bme_err = bme280_read(&s_bme, &bme_data);

            ds1307_time_t rtc_time;
            esp_err_t rtc_err = ds1307_read_time(&s_rtc, &rtc_time);

            xSemaphoreGive(xI2CMutex);

            if (bme_err == ESP_OK) {
                sample.temperature_c = bme_data.temperature_c;
                sample.humidity_pct = bme_data.humidity_pct;
                sample.pressure_hpa = bme_data.pressure_hpa;
            } else {
                ESP_LOGW(TAG, "BME280 read failed: %s", esp_err_to_name(bme_err));
            }

            if (rtc_err == ESP_OK) {
                sample.year = rtc_time.year;
                sample.month = rtc_time.month;
                sample.day = rtc_time.day;
                sample.hour = rtc_time.hour;
                sample.minute = rtc_time.minute;
                sample.second = rtc_time.second;
                sample.unix_time = ds1307_to_unix(&rtc_time);
            } else {
                ESP_LOGW(TAG, "DS1307 read failed: %s", esp_err_to_name(rtc_err));
                sample.unix_time = (uint32_t)(esp_timer_get_time() / 1000000);
            }

            sample.valid = (bme_err == ESP_OK) && (rtc_err == ESP_OK);
        } else {
            ESP_LOGW(TAG, "I2C mutex timeout");
        }

        xQueueSend(xSensorQueue, &sample, portMAX_DELAY);
    }
}
