#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC";

// ── ADC config ────────────────────────────────────────────────────────────────
#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_9    // GPIO10 on ESP32-S3 → ADC1 channel 9
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12
#define ADC_MAX_RAW     4095
// Effective full-scale for 12 dB attenuation on ESP32-S3 (used in manual formula)
#define VMAX_MV         3100.0f
#define READ_PERIOD_MS  100

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok = false;

// ── Calibration init ─────────────────────────────────────────────────────────
static void cali_init(void)
{
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    const adc_cali_line_fitting_config_t cfg = {
        .unit_id  = ADC_UNIT,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    s_cali_ok = (adc_cali_create_scheme_line_fitting(&cfg, &s_cali) == ESP_OK);
    if (s_cali_ok) ESP_LOGI(TAG, "Calibration scheme: line-fitting");
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
        if (s_cali_ok) ESP_LOGI(TAG, "Calibration scheme: curve-fitting");
    }
#endif

    if (!s_cali_ok) ESP_LOGW(TAG, "No calibration — U_cali column will show N/A");
}

// ── Entry point ───────────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    // Print configuration table on startup
    printf("\n");
    printf("+--------------------------+----------------------------+\n");
    printf("| Parameter                | Value                      |\n");
    printf("+--------------------------+----------------------------+\n");
    printf("| ADC unit / channel       | ADC1 / CH9  (GPIO10)       |\n");
    printf("| Resolution               | 12-bit  (0 - 4095)         |\n");
    printf("| Attenuation              | 12 dB                      |\n");
    printf("| V_ref (manual formula)   | 3100 mV  (typ. ESP32-S3)   |\n");
    printf("| Calibration              | eFuse line-fitting / curve |\n");
    printf("| Read interval            | 100 ms                     |\n");
    printf("+--------------------------+----------------------------+\n\n");

    // ── ADC oneshot init ──────────────────────────────────────────────────────
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

    // ── Table header ─────────────────────────────────────────────────────────
    printf("%-6s  %-14s  %-12s  %-10s\n",
           "RAW", "U_manual(mV)", "U_cali(mV)", "Error(%)");
    printf("--------------------------------------------------\n");

    // ── Main sampling loop ────────────────────────────────────────────────────
    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));

        // Linear formula using assumed V_max
        const float u_manual = (float)raw / ADC_MAX_RAW * VMAX_MV;

        int   u_cali  = 0;
        float err_pct = 0.0f;

        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali, raw, &u_cali);
            if (u_cali > 0) {
                err_pct = fabsf(u_manual - (float)u_cali) / (float)u_cali * 100.0f;
            }
            printf("%-6d  %-14.1f  %-12d  %-10.2f\n",
                   raw, u_manual, u_cali, err_pct);
        } else {
            printf("%-6d  %-14.1f  %-12s  %-10s\n",
                   raw, u_manual, "N/A", "N/A");
        }

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
