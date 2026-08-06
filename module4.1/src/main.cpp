/*
 * Module 4.1 — ESP32 <-> STM32 UART Button/LED Exchange
 *
 * Board       : ESP32-S3-DevKitC-1
 * STM32 board : STM32F401 (Core/Src/main.c, USART1 @ PA9/PA10, DMA)
 *
 * Wiring:
 *   ESP32 GPIO17 (TX) -> STM32 PA10 (USART1_RX)
 *   ESP32 GPIO18 (RX) -> STM32 PA9  (USART1_TX)
 *   ESP32 GND         -> STM32 GND
 *
 *   ESP32 button -> GPIO6 (pull-up, active LOW)
 *   ESP32 LED    -> GPIO1
 *   STM32 button -> PA1 (pull-up, active LOW)
 *   STM32 LED    -> PA2
 *
 * ── Task ─────────────────────────────────────────────────────────────────
 *   Pressing the button on either board toggles the LED on the *other*
 *   board. Both directions share the same single-byte protocol: sending
 *   BUTTON_EVENT_BYTE over UART tells the receiving side to toggle its LED.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

static const char *TAG = "ESP_STM_LINK";

// ── GPIO config ──────────────────────────────────────────────────────────
#define BUTTON_PIN      GPIO_NUM_6
#define LED_PIN         GPIO_NUM_1
#define DEBOUNCE_MS     50

// ── UART config (link to STM32) ─────────────────────────────────────────
#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_18
#define UART_BAUD_RATE  115200
#define UART_BUF_SIZE   256

#define BUTTON_EVENT_BYTE '\x42' // 'B'

static void gpio_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_PIN, 0);

    gpio_config_t button_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_cfg));
}

static void uart_init(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// Blocks waiting for UART bytes; on BUTTON_EVENT_BYTE toggles the local LED.
static void uart_rx_task(void *arg)
{
    uint8_t rx_byte = 0;
    int led_level = 0; // gpio_get_level() can't read back a GPIO_MODE_OUTPUT pin, so track state ourselves

    while (true) {
        int len = uart_read_bytes(UART_PORT, &rx_byte, 1, pdMS_TO_TICKS(100));
        if (len > 0 && rx_byte == BUTTON_EVENT_BYTE) {
            led_level = !led_level;
            gpio_set_level(LED_PIN, led_level);
            ESP_LOGI(TAG, "STM32 button pressed -> local LED = %d", led_level);
        }
    }
}

extern "C" void app_main(void)
{
    gpio_init();
    uart_init();

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);

    int last_button_level = 1; // pull-up: idle = HIGH
    TickType_t last_debounce_tick = 0;

    while (true) {
        int level = gpio_get_level(BUTTON_PIN);

        if (level != last_button_level &&
            (xTaskGetTickCount() - last_debounce_tick) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_debounce_tick = xTaskGetTickCount();
            last_button_level = level;

            if (level == 0) { // pressed: pull-up input goes LOW
                uint8_t tx_byte = BUTTON_EVENT_BYTE;
                uart_write_bytes(UART_PORT, (const char *)&tx_byte, 1);
                ESP_LOGI(TAG, "Local button pressed -> sent to STM32");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
