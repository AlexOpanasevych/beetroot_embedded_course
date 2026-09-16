/*
 * Miniproject Module 6 — FreeRTOS environment logger
 *
 * Combines everything asked for: BME280 (temperature/humidity/pressure) and
 * DS1307 (time) read over a shared I2C bus, an SSD1306 status display, a
 * binary log periodically appended to a dedicated "logdata" flash
 * partition, UART commands, and automatic light sleep between cycles.
 *
 * Five FreeRTOS tasks, wired together with queues (data handoff) and one
 * mutex (shared I2C bus):
 *
 *   Sensor Task    (sensor_task.cpp)    — reads BME280 + DS1307
 *   UART Task      (uart_task.cpp)      — parses console commands
 *   Display Task   (display_task.cpp)   — renders the SSD1306
 *   Manager Task   (manager_task.cpp)   — cycle timing, logging policy
 *   Flash Writer   (flash_log.cpp)      — buffers/writes the "logdata" partition
 *
 * Board  : ESP32-S3-DevKitC-1
 * Wiring : all three I2C devices share one bus —
 *          GPIO8 (SDA), GPIO9 (SCL), 3V3, GND
 *          BME280 @ 0x76, DS1307 @ 0x68, SSD1306 0.96" 128x64 @ 0x3C
 *
 * Note: built and verified to compile in this environment; no physical
 * board/sensors were attached to test on hardware here, so please confirm
 * wiring, timing and power-consumption behavior on the bench.
 */

#include "common.h"
#include "display.h"
#include "flash_log.h"
#include "esp_pm.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

QueueHandle_t     xSensorQueue;
QueueHandle_t     xDisplayQueue;
QueueHandle_t     xFlashQueue;
QueueHandle_t     xUartCmdQueue;
SemaphoreHandle_t xI2CMutex;
TaskHandle_t      xSensorTaskHandle;

static i2c_master_bus_handle_t s_i2c_bus;

static void i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));
}

// Automatic (tickless) light sleep: whenever every task in the system is
// blocked -- which is most of each CYCLE_PERIOD_MS window, since Sensor /
// Display / FlashWriter all wait on queues or notifications at 0% CPU and
// Manager itself is parked in vTaskDelay() -- FreeRTOS' tickless idle hook
// lets the SoC drop into light sleep on its own, and wakes it again for the
// next scheduled tick. That satisfies "wake once a second, sleep the rest
// of the time" without a manual esp_light_sleep_start() call, which would
// have to suspend every other task (UART included) itself.
//
// Documented limitation: light sleep here has no UART wakeup source
// configured, so a keystroke that lands exactly while the chip is asleep
// is only picked up on the next scheduled wake rather than instantly.
static void power_management_init(void)
{
    esp_pm_config_t pm_cfg = {};
    pm_cfg.max_freq_mhz = 160;
    pm_cfg.min_freq_mhz = 40;
    pm_cfg.light_sleep_enable = true;

    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure failed: %s (is CONFIG_PM_ENABLE set?)", esp_err_to_name(err));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Miniproject module6: FreeRTOS environment logger starting");

    i2c_bus_init();

    xI2CMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(sensor_sample_t));
    xDisplayQueue = xQueueCreate(1, sizeof(display_msg_t));
    xFlashQueue = xQueueCreate(4, sizeof(flash_msg_t));
    xUartCmdQueue = xQueueCreate(5, sizeof(uart_cmd_t));
    if (!xI2CMutex || !xSensorQueue || !xDisplayQueue || !xFlashQueue || !xUartCmdQueue) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        abort();
    }

    sensor_task_init(s_i2c_bus);
    display_init(s_i2c_bus);
    ESP_ERROR_CHECK(flashlog_init());

    power_management_init();

    xTaskCreate(sensor_task, "Sensor", 4096, NULL, 5, &xSensorTaskHandle);
    xTaskCreate(display_task, "Display", 4096, NULL, 4, NULL);
    xTaskCreate(uart_task, "Uart", 4096, NULL, 4, NULL);
    xTaskCreate(flash_writer_task, "FlashWriter", 4096, NULL, 4, NULL);
    xTaskCreate(manager_task, "Manager", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "All tasks started");
}
