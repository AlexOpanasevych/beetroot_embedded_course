/*
 * miniproject_module6 — shared config, message types and RTOS handles.
 *
 * Five tasks share three kinds of data: sensor readings (Sensor -> Manager),
 * display frames (Manager -> Display) and flash write requests
 * (Manager -> FlashWriter), plus a stream of parsed UART commands
 * (Uart -> Manager). Every one of those handoffs goes through a queue; the
 * only piece of hardware three different tasks touch directly (BME280,
 * DS1307, SSD1306 all live on the same I2C bus) is guarded by xI2CMutex.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

// ── Hardware ─────────────────────────────────────────────────────────────
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_PIN         GPIO_NUM_8
#define I2C_SCL_PIN         GPIO_NUM_9
#define BME280_I2C_ADDR     0x76   // 0x77 if the breakout's SDO is tied to VCC
#define DS1307_I2C_ADDR     0x68

// ── Timing ───────────────────────────────────────────────────────────────
#define CYCLE_PERIOD_MS      1000   // wake / read sensors / update display / sleep
#define LOG_INTERVAL_MINUTES 5      // how often a sample is appended to flash

// ── Flash logging ────────────────────────────────────────────────────────
#define LOGDATA_PARTITION_NAME "logdata"
#define FLASH_SECTOR_SIZE       4096

// ── UART command line ───────────────────────────────────────────────────
#define UART_CMD_LINE_MAX   32

// ── Sensor sample (Sensor task -> Manager task) ─────────────────────────
typedef struct {
    bool     valid;         // both BME280 and DS1307 were read successfully
    uint32_t unix_time;      // seconds since epoch, derived from the RTC
    uint8_t  year;           // 0 == 2000, as stored by the DS1307
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    float    temperature_c;
    float    humidity_pct;
    float    pressure_hpa;
} sensor_sample_t;

// ── Display frame (Manager task -> Display task) ────────────────────────
typedef struct {
    sensor_sample_t sample;
    bool    logging_enabled;
    bool    flash_full;
    uint8_t flash_pct; // 0-100
} display_msg_t;

// ── Binary log record, exactly as written to the "logdata" partition ────
#pragma pack(push, 1)
typedef struct {
    uint32_t timestamp;          // DS1307, unix seconds
    int16_t  temp_c_x100;        // temperature (C) * 100
    uint16_t humidity_pct_x100;  // relative humidity (%) * 100
    uint16_t pressure_mmhg;      // pressure, mmHg
} log_entry_t;
#pragma pack(pop)

// ── Flash Writer requests (Manager task -> FlashWriter task) ───────────
typedef enum {
    FLASH_OP_APPEND,
    FLASH_OP_FLUSH,
    FLASH_OP_ERASE,
} flash_op_t;

typedef struct {
    flash_op_t op;
    log_entry_t entry; // only meaningful for FLASH_OP_APPEND
} flash_msg_t;

// ── Parsed UART commands (Uart task -> Manager task) ────────────────────
typedef enum {
    CMD_STATUS,
    CMD_LOG_ON,
    CMD_LOG_OFF,
    CMD_FLUSH,
    CMD_ERASE,
    CMD_UNKNOWN,
} uart_cmd_t;

// ── Shared RTOS handles (defined in main.cpp) ───────────────────────────
extern QueueHandle_t     xSensorQueue;    // sensor_sample_t, length 1 (latest wins)
extern QueueHandle_t     xDisplayQueue;   // display_msg_t,  length 1 (latest wins)
extern QueueHandle_t     xFlashQueue;     // flash_msg_t,    length 4
extern QueueHandle_t     xUartCmdQueue;   // uart_cmd_t,     length 5
extern SemaphoreHandle_t xI2CMutex;       // guards the shared I2C bus
extern TaskHandle_t      xSensorTaskHandle;

// ── Task entry points ────────────────────────────────────────────────────
esp_err_t sensor_task_init(i2c_master_bus_handle_t bus);
void sensor_task(void *pv);
void display_task(void *pv);
void uart_task(void *pv);
void manager_task(void *pv);
void flash_writer_task(void *pv);
