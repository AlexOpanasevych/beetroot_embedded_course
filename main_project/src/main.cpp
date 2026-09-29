/*
 * Main project — "Desk Guardian: Networked Sentry"
 *
 * Builds on the miniproject_module5 Desk Guardian PCB (PIR + buzzer +
 * LED eyes on an ESP32-S3) and adds:
 *   - a BME280 (temperature/humidity/pressure) over I2C, same part and
 *     driver logic as module4.4's STM32 telemetry node, ported to
 *     ESP-IDF's driver/i2c_master.h (see module4.2's OLED ticker for that
 *     same driver style).
 *   - the PID fan controller from module5.6, now driven by the BME280
 *     temperature instead of a thermistor.
 *   - the multi-producer/single-consumer FreeRTOS queue pattern from
 *     module6.3 (sensor_hub.cpp) fanning BME280 + PIR events into one
 *     aggregator task.
 *   - the "sleep at 0% CPU on a binary semaphore until an ISR wakes you"
 *     pattern from module6.5, reused for the PIR input (pir_alarm.cpp).
 *   - the Wi-Fi STA connect pattern from miniproject_module3's subwoofer
 *     amp, now serving a small built-in status dashboard instead of
 *     streaming audio.
 *
 * Wiring (Desk Guardian PCB pins unchanged, new sensors/actuator on
 * breadboard/jumpers — see README.md for the full table):
 *   IO4  - PIR_IN (existing, RC-filtered on the board)
 *   IO5  - buzzer control, via existing transistor driver
 *   IO6  - LED eye 1
 *   IO7  - LED eye 2
 *   IO8  - I2C SDA -> BME280
 *   IO9  - I2C SCL -> BME280
 *   IO18 - fan PWM, via a transistor + flyback diode (same technique as
 *          the board's own buzzer driver, breadboarded this time)
 */
#include "bme280.h"
#include "sensor_hub.h"
#include "pir_alarm.h"
#include "fan_pid.h"
#include "wifi_sta.h"
#include "web_dashboard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "MAIN_PROJECT";

constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_9;
constexpr uint32_t   BME280_POLL_MS = 2000;

static bme280_handle_t s_bme280;
static bool            s_bme280_ready = false;

static void bme280_task(void *pvParameters)
{
    while (1) {
        sensor_msg_t msg = {};
        msg.src = SRC_BME280;
        msg.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (s_bme280_ready &&
            bme280_read(&s_bme280, &msg.temperature_c, &msg.humidity_pct, &msg.pressure_hpa)) {
            msg.bme280_ok = true;
        } else {
            msg.bme280_ok = false;
        }

        sensor_hub_post(&msg);
        vTaskDelay(pdMS_TO_TICKS(BME280_POLL_MS));
    }
}

static void i2c_and_bme280_init(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = I2C_SDA_PIN;
    bus_cfg.scl_io_num = I2C_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    s_bme280_ready = bme280_init(&s_bme280, bus);
    if (!s_bme280_ready) {
        ESP_LOGE(TAG, "BME280 init failed - check wiring/address (SDO->GND for 0x76)");
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sensor_hub_init();
    i2c_and_bme280_init();
    pir_alarm_init();
    fan_pid_init();

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    wifi_sta_init();
    web_dashboard_start();

    xTaskCreate(bme280_task, "Bme280Task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Desk Guardian sentry running.");
}
