/*
 * Module 4.2 — SSD1306 OLED Ticker
 *
 * Board   : ESP32-S3-DevKitC-1
 * Display : SSD1306 0.96" OLED, 128x64, I2C
 * Library : k0i05/esp_ssd1306 (pure ESP-IDF, no Arduino dependency)
 *
 * Wiring:
 *   ESP32 GPIO8 (SDA) -> SSD1306 SDA
 *   ESP32 GPIO9 (SCL) -> SSD1306 SCL
 *   ESP32 3V3         -> SSD1306 VCC
 *   ESP32 GND         -> SSD1306 GND
 *
 * ── Task ─────────────────────────────────────────────────────────────────
 *   Scroll a string across the full width of the 128x64 display using the
 *   library's built-in ticker helper (text enters from the right, scrolls
 *   left across the box, then exits).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"

static const char *TAG = "SSD1306_TICKER";

// ── I2C config ───────────────────────────────────────────────────────────
constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_9;

// ── Ticker config ────────────────────────────────────────────────────────
constexpr uint8_t TICKER_PAGE = 3;     // page 3 of 8 -> vertically centered row
constexpr uint8_t TICKER_SEGMENT = 0;     // start at the left edge
constexpr uint8_t TICKER_BOX_WIDTH = 16;    // 16 * 8px chars = full 128px width
constexpr uint32_t TICKER_STEP_MS = 15;    // delay per pixel-column shift

static i2c_master_bus_handle_t s_i2c_bus;
static ssd1306_handle_t s_ssd1306;

static void i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));
}

extern "C" void app_main(void)
{
    i2c_init();

    ssd1306_config_t dev_cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    ESP_ERROR_CHECK(ssd1306_init(s_i2c_bus, &dev_cfg, &s_ssd1306));
    ESP_ERROR_CHECK(ssd1306_clear_display(s_ssd1306, false));

    ESP_LOGI(TAG, "Starting ticker");

    while (true) {
        // ssd1306_display_textbox_ticker caps text at 50 chars (SSD1306_TEXTBOX_DISPLAY_MAX_LEN);
        // longer strings return ESP_ERR_INVALID_SIZE and abort via ESP_ERROR_CHECK.
        ESP_ERROR_CHECK(ssd1306_display_textbox_ticker(
            s_ssd1306, TICKER_PAGE, TICKER_SEGMENT,
            "Hello from ESP32-S3! Module 4.2 ticker demo... ",
            TICKER_BOX_WIDTH, false, TICKER_STEP_MS));

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
