#include <cstring>
#include <cstdint>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "spi_env_telemetry";

// Same SPI wiring/timing as the module 4.3 mini-project (see
// beetroot_embedded_course/miniproject_module4/src/main.cpp) -- this board is still
// the ESP32 SPI2 master talking to the STM32F401 SPI1 hardware-NSS slave.
constexpr gpio_num_t PIN_SCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_MISO = GPIO_NUM_13;
constexpr gpio_num_t PIN_CS = GPIO_NUM_10;

constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
constexpr int SPI_CLOCK_HZ = 200 * 1000; // 200 kHz -- matches the breadboard wiring margin from module 4.3

// ----------------------------------------------------------------------------
// Custom SPI telemetry protocol (STM32 slave -> ESP32 master), module 4.4/4.5.
// Mirrors the frame-layout comment in module4.4/Core/Src/main.c -- keep both in sync.
//
// Byte 0 of every frame in both directions is a throwaway dummy (STM32 hardware-NSS
// SPI slave mode doesn't reliably shift the first byte right after NSS asserts).
//
// REQUEST (ESP32 -> STM32):
//   [0] dummy, [1] SYNC_BYTE(0xA5), [2] CMD_GET_ENV(0x30), [3] arg(0x00),
//   [4] checksum = frame[1]^frame[2]^frame[3], [5..19] padding (0x00)
//
// RESPONSE (STM32 -> ESP32), one SPI transaction behind (preloaded by the slave
// while servicing the previous transaction):
//   [0] dummy
//   [1] ACK_BYTE (0x5A if the STM32 accepted our request, 0x00 otherwise)
//   [2] year (uint8, offset from 2000)      [3] month   [4] day
//   [5] hour                                 [6] minute  [7] second
//   [8..9]   temperature_x100  int16 BE  (degC * 100)
//   [10..11] humidity_x100     uint16 BE (%RH * 100)
//   [12..13] pressure_x10      uint16 BE (hPa * 10)
//   [14] light_percent (uint8, 0-100)
//   [15] status: bit0 = BME280 ok, bit1 = RTC ok
//   [16] checksum = XOR of frame[1..15]
//   [17..19] padding (0x00)
// ----------------------------------------------------------------------------
constexpr size_t FRAME_LEN = 20;

constexpr uint8_t SYNC_BYTE = 0xA5;
constexpr uint8_t ACK_BYTE = 0x5A;
constexpr uint8_t CMD_GET_ENV = 0x30;

struct EnvReading {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    uint8_t light_percent;
    bool bme280_ok;
    bool rtc_ok;
};

spi_device_handle_t stm32_handle;

void spi_master_init() {
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = PIN_SCLK;
    bus_cfg.mosi_io_num = PIN_MOSI;
    bus_cfg.miso_io_num = PIN_MISO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = FRAME_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = SPI_CLOCK_HZ;
    dev_cfg.mode = 0; // CPOL=0, CPHA=0 -- matches the STM32 SPI1 slave config
    dev_cfg.spics_io_num = PIN_CS;
    dev_cfg.queue_size = 1;
    // Keep CS asserted a few bit-cycles past the last clock edge so the STM32 slave
    // has enough hold time to latch the final bit before NSS deasserts.
    dev_cfg.cs_ena_posttrans = 8;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev_cfg, &stm32_handle));
}

esp_err_t spi_exchange(const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {};
    t.length = len * 8; // bits
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_transmit(stm32_handle, &t);
}

void build_request(uint8_t *tx_buf) {
    memset(tx_buf, 0, FRAME_LEN);
    tx_buf[1] = SYNC_BYTE;
    tx_buf[2] = CMD_GET_ENV;
    tx_buf[3] = 0x00;
    tx_buf[4] = static_cast<uint8_t>(tx_buf[1] ^ tx_buf[2] ^ tx_buf[3]);
}

// Returns false if the response failed the ACK/checksum check (stale link, corrupted
// frame, or the STM32 hasn't serviced a request yet).
bool parse_response(const uint8_t *rx_buf, EnvReading *out) {
    uint8_t checksum = 0;
    for (int i = 1; i <= 15; i++) {
        checksum ^= rx_buf[i];
    }
    if (rx_buf[1] != ACK_BYTE || rx_buf[16] != checksum) {
        return false;
    }

    out->year = 2000 + rx_buf[2];
    out->month = rx_buf[3];
    out->day = rx_buf[4];
    out->hour = rx_buf[5];
    out->minute = rx_buf[6];
    out->second = rx_buf[7];

    int16_t temp_x100 = static_cast<int16_t>((rx_buf[8] << 8) | rx_buf[9]);
    uint16_t hum_x100 = static_cast<uint16_t>((rx_buf[10] << 8) | rx_buf[11]);
    uint16_t press_x10 = static_cast<uint16_t>((rx_buf[12] << 8) | rx_buf[13]);

    out->temperature_c = temp_x100 / 100.0f;
    out->humidity_pct = hum_x100 / 100.0f;
    out->pressure_hpa = press_x10 / 10.0f;
    out->light_percent = rx_buf[14];
    out->bme280_ok = (rx_buf[15] & 0x01) != 0;
    out->rtc_ok = (rx_buf[15] & 0x02) != 0;

    return true;
}

void print_reading(const EnvReading &r) {
    ESP_LOGI(TAG,
        "\n"
        "+-------------------------------------------+\n"
        "|            STM32 ENVIRONMENT DATA          |\n"
        "+-------------------------------------------+\n"
        "| Date/Time  : %04u-%02u-%02u  %02u:%02u:%02u          |\n"
        "| Temperature: %6.2f C                      |\n"
        "| Humidity   : %6.2f %%RH                    |\n"
        "| Pressure   : %7.1f hPa                    |\n"
        "| Light level: %3u %%                         |\n"
        "| Sensors    : BME280=%-3s  RTC=%-3s           |\n"
        "+-------------------------------------------+",
        r.year, r.month, r.day, r.hour, r.minute, r.second,
        r.temperature_c, r.humidity_pct, r.pressure_hpa, r.light_percent,
        r.bme280_ok ? "OK" : "ERR", r.rtc_ok ? "OK" : "ERR");
}

} // namespace

extern "C" void app_main() {
    spi_master_init();
    ESP_LOGI(TAG, "SPI master ready (host=%d, sclk=%d mosi=%d miso=%d cs=%d)",
             SPI_HOST, PIN_SCLK, PIN_MOSI, PIN_MISO, PIN_CS);

    uint8_t tx_buf[FRAME_LEN];
    uint8_t rx_buf[FRAME_LEN];

    while (true) {
        build_request(tx_buf);
        memset(rx_buf, 0, sizeof(rx_buf));

        esp_err_t err = spi_exchange(tx_buf, rx_buf, FRAME_LEN);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
        } else {
            EnvReading reading{};
            if (parse_response(rx_buf, &reading)) {
                print_reading(reading);
            } else {
                ESP_LOGW(TAG, "no valid frame yet (rx[1]=%02X rx[16]=%02X) -- STM32 side may still be booting",
                         rx_buf[1], rx_buf[16]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
