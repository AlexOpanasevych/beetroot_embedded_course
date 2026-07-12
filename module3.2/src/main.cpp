#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

static const char *TAG = "LDR";

// ── Hardware ──────────────────────────────────────────────────────────────────
// Wiring: VCC → LDR → GPIO9 → 10 kΩ → GND  (LDR on high side)
// More light → lower LDR resistance → lower voltage → lower raw ADC value
constexpr auto ADC_UNIT        = ADC_UNIT_1;
constexpr auto ADC_CHANNEL     = ADC_CHANNEL_8;   // GPIO9 on ESP32-S3
constexpr auto ADC_ATTEN       = ADC_ATTEN_DB_12;
constexpr auto ADC_BITWIDTH    = ADC_BITWIDTH_12;

constexpr auto LED_GPIO        = GPIO_NUM_4;

// ── SMA filter ────────────────────────────────────────────────────────────────
constexpr auto SMA_SIZE        = 16;              // 16 × 50 ms = 800 ms window

// ── Hysteresis thresholds (raw ADC, 0–4095) ───────────────────────────────────
// High raw  → dark;  low raw → bright  (LDR pull-down topology)
constexpr auto DARK_THRESHOLD  = 2500;            // SMA rises above → LED ON
constexpr auto LIGHT_THRESHOLD = 2000;            // SMA falls below → LED OFF

constexpr auto READ_PERIOD_MS  = 50;

// ── Globals ───────────────────────────────────────────────────────────────────
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok = false;

static int  s_buf[SMA_SIZE];
static int  s_idx   = 0;
static long s_sum   = 0;
static int  s_count = 0;

// ── Calibration ───────────────────────────────────────────────────────────────
static void cali_init(void)
{
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    {
        const adc_cali_line_fitting_config_t cfg = {
            .unit_id  = ADC_UNIT,
            .atten    = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH,
        };
        s_cali_ok = (adc_cali_create_scheme_line_fitting(&cfg, &s_cali) == ESP_OK);
        if (s_cali_ok) ESP_LOGI(TAG, "Calibration: line-fitting");
    }
#endif

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!s_cali_ok) {
        const adc_cali_curve_fitting_config_t cfg = {
            .unit_id  = ADC_UNIT,
            .chan     = ADC_CHANNEL,
            .atten    = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH,
        };
        s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cfg, &s_cali) == ESP_OK);
        if (s_cali_ok) ESP_LOGI(TAG, "Calibration: curve-fitting");
    }
#endif

    if (!s_cali_ok) ESP_LOGW(TAG, "No calibration — voltage column will be skipped");
}

// ── Simple Moving Average ─────────────────────────────────────────────────────
static int sma_update(int raw)
{
    s_sum -= s_buf[s_idx];
    s_buf[s_idx] = raw;
    s_sum += raw;
    s_idx = (s_idx + 1) % SMA_SIZE;
    if (s_count < SMA_SIZE) s_count++;
    return (int)(s_sum / s_count);
}

// ── Entry point ───────────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    // LED init
    const gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_GPIO, 0);

    // ADC oneshot init
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT,
        .clk_src  = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL, &chan_cfg));

    cali_init();

    ESP_LOGI(TAG, "Started — dark>%d / light<%d, SMA window=%d samples",
             DARK_THRESHOLD, LIGHT_THRESHOLD, SMA_SIZE);

    bool led_on = false;

    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));
        const int sma = sma_update(raw);

        // Hysteresis: only switch state when SMA crosses the respective threshold
        if (!led_on && sma > DARK_THRESHOLD) {
            led_on = true;
            gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "DARK  → LED ON  (sma=%d)", sma);
        } else if (led_on && sma < LIGHT_THRESHOLD) {
            led_on = false;
            gpio_set_level(LED_GPIO, 0);
            ESP_LOGI(TAG, "LIGHT → LED OFF (sma=%d)", sma);
        }

        if (s_cali_ok) {
            int mv = 0;
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
            ESP_LOGI(TAG, "raw=%4d  sma=%4d  %4d mV  %s",
                     raw, sma, mv, led_on ? "[ON]" : "[off]");
        } else {
            ESP_LOGI(TAG, "raw=%4d  sma=%4d  %s",
                     raw, sma, led_on ? "[ON]" : "[off]");
        }

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
