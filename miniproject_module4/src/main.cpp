#include <cstring>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "spi_stm32";

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

} // namespace

extern "C" void app_main() {
    spi_master_init();
    ESP_LOGI(TAG, "SPI master ready (host=%d, sclk=%d mosi=%d miso=%d cs=%d)",
             SPI_HOST, PIN_SCLK, PIN_MOSI, PIN_MISO, PIN_CS);

    uint8_t tx_buf[FRAME_LEN];
    uint8_t rx_buf[FRAME_LEN];
    bool led_on = false;

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
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
        } else if (rx_buf[1] == ACK_BYTE && rx_buf[5] == resp_checksum) {
            ESP_LOGI(TAG, "sent led=%u -> ack cmd=%02X applied=%u status=%u",
                     tx_buf[3], rx_buf[2], rx_buf[3], rx_buf[4]);
        } else {
            ESP_LOGW(TAG, "sent led=%u -> no valid ack yet rx=[%02X %02X %02X %02X %02X %02X]",
                     tx_buf[3], rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4], rx_buf[5]);
        }

        led_on = !led_on; // toggle PB0 on/off each cycle
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
