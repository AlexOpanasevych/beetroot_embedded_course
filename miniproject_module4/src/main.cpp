/*
 * Miniproject module 4 — SPI-controlled remote LED with an I2C status display
 *
 * ESP32-S3 is master on two independent buses:
 *   - SPI2: framed command/ACK link to an STM32F401 slave, toggling its PB0 LED.
 *   - I2C0: drives an SSD1306 OLED that mirrors the link's live state, so the
 *     STM32 doesn't need a screen or extra wiring to observe what's happening.
 */

#include <cstdio>
#include <cstring>

#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"

namespace {

constexpr char TAG[] = "spi_stm32";

// ── SPI (link to STM32) ─────────────────────────────────────────────────────
constexpr gpio_num_t PIN_SCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_MISO = GPIO_NUM_13;
constexpr gpio_num_t PIN_CS = GPIO_NUM_10;

constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
constexpr int SPI_CLOCK_HZ = 200 * 1000; // 200 kHz -- 1 MHz was too fast for this breadboard wiring's signal margin

constexpr size_t FRAME_LEN = 8;

constexpr uint8_t SYNC_BYTE = 0xA5; // master -> slave request marker
constexpr uint8_t ACK_BYTE = 0x5A;  // slave -> master response marker
constexpr uint8_t CMD_SET_LED = 0x10; // argument: 0 -> PB0 low, nonzero -> PB0 high

spi_device_handle_t stm32_handle;

// ── I2C (status display) ────────────────────────────────────────────────────
constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_9;

i2c_master_bus_handle_t i2c_bus;
ssd1306_handle_t oled;

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
    dev_cfg.mode = 0; // CPOL=0, CPHA=0 -- match the STM32 SPI config
    dev_cfg.spics_io_num = PIN_CS;
    dev_cfg.queue_size = 1;
    // Keep CS asserted a few bit-cycles past the last clock edge so the STM32
    // slave has enough hold time to latch the final bit before NSS deasserts.
    dev_cfg.cs_ena_posttrans = 8;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev_cfg, &stm32_handle));
}

// Full-duplex exchange: STM32 slave must have TX data preloaded before ESP32 asserts CS.
esp_err_t spi_exchange(const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {};
    t.length = len * 8; // bits
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_transmit(stm32_handle, &t);
}

void i2c_display_init() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    ssd1306_config_t dev_cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ESP_ERROR_CHECK(ssd1306_init(i2c_bus, &dev_cfg, &oled));
    ESP_ERROR_CHECK(ssd1306_clear_display(oled, false));
}

void display_status(bool led_on, bool link_ok, uint32_t tx_count, uint32_t err_count) {
    char line[32];

    ssd1306_clear_display(oled, false);

    snprintf(line, sizeof(line), "STM32 SPI link");
    ssd1306_display_text(oled, 0, line, false);

    snprintf(line, sizeof(line), "LED cmd: %s", led_on ? "ON" : "OFF");
    ssd1306_display_text(oled, 2, line, false);

    snprintf(line, sizeof(line), "Link: %s", link_ok ? "OK" : "FAIL");
    ssd1306_display_text(oled, 4, line, false);

    snprintf(line, sizeof(line), "tx=%lu err=%lu",
             static_cast<unsigned long>(tx_count), static_cast<unsigned long>(err_count));
    ssd1306_display_text(oled, 6, line, false);
}

} // namespace

extern "C" void app_main() {
    spi_master_init();
    ESP_LOGI(TAG, "SPI master ready (host=%d, sclk=%d mosi=%d miso=%d cs=%d)",
             SPI_HOST, PIN_SCLK, PIN_MOSI, PIN_MISO, PIN_CS);

    i2c_display_init();
    ESP_LOGI(TAG, "I2C status display ready (sda=%d scl=%d)", I2C_SDA_PIN, I2C_SCL_PIN);

    uint8_t tx_buf[FRAME_LEN];
    uint8_t rx_buf[FRAME_LEN];
    bool led_on = false;
    uint32_t tx_count = 0;
    uint32_t err_count = 0;

    while (true) {
        memset(tx_buf, 0, sizeof(tx_buf));
        memset(rx_buf, 0, sizeof(rx_buf));
        // tx_buf[0] is a throwaway dummy byte -- see the framing comment in the STM32
        // firmware's Process_SPI_Command for why byte 0 is never trusted on either side.
        tx_buf[1] = SYNC_BYTE;
        tx_buf[2] = CMD_SET_LED;
        tx_buf[3] = led_on ? 1 : 0;
        tx_buf[4] = static_cast<uint8_t>(tx_buf[1] ^ tx_buf[2] ^ tx_buf[3]); // request checksum

        esp_err_t err = spi_exchange(tx_buf, rx_buf, FRAME_LEN);
        uint8_t resp_checksum = static_cast<uint8_t>(rx_buf[1] ^ rx_buf[2] ^ rx_buf[3] ^ rx_buf[4]);
        tx_count++;

        bool link_ok = (err == ESP_OK) && (rx_buf[1] == ACK_BYTE) && (rx_buf[5] == resp_checksum);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
            err_count++;
        } else if (link_ok) {
            ESP_LOGI(TAG, "sent led=%u -> ack cmd=%02X applied=%u status=%u",
                     tx_buf[3], rx_buf[2], rx_buf[3], rx_buf[4]);
        } else {
            ESP_LOGW(TAG, "sent led=%u -> no valid ack yet rx=[%02X %02X %02X %02X %02X %02X]",
                     tx_buf[3], rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5]);
            err_count++;
        }

        display_status(led_on, link_ok, tx_count, err_count);

        led_on = !led_on; // toggle PB0 on/off each cycle
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
