/*
 * Module 5.5 — ADC + two software filters, compared
 *
 * Base: the plain ADC-reading code from module3.1 (raw pot reading + calibrated
 * mV, no filtering at all). This module keeps that ADC/calibration setup as-is
 * and adds two independent software filters on top of the same raw stream:
 *
 *   1. SMA — Simple Moving Average, fixed-size window, unweighted.
 *   2. EMA — Exponential Moving Average, weighted toward recent samples.
 *
 * Both are tuned to roughly the same *effective* averaging length (~10 samples)
 * so the comparison is apples-to-apples: for EMA, effective window length is
 * N_eff = (2 - alpha) / alpha, so alpha = 0.2 -> N_eff ~= 9, close to the SMA's
 * fixed window of 10. Same amount of "smoothing budget", different shape of
 * impulse response — SMA has a hard cutoff (all-or-nothing box window), EMA
 * decays gradually (more weight on the newest samples).
 *
 * Every sample is printed as one table row (RAW / SMA / EMA, in raw counts and
 * in calibrated mV). Every NOISE_WINDOW samples a STATS line reports the mean
 * sample-to-sample jump (jitter) for each of the three signals — a simple,
 * objective noise measure: hold the pot still to compare noise suppression,
 * sweep it to feel the lag each filter introduces.
 *
 * Board       : ESP32-S3-DevKitC-1
 * Potentiometer: wiper -> GPIO10 (ADC1 channel 9)
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC_FILTERS";

// ── ADC config (unchanged from module3.1) ──────────────────────────────────
#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_9    // GPIO10 on ESP32-S3 -> ADC1 channel 9
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12
#define READ_PERIOD_MS  50

// ── SMA filter ───────────────────────────────────────────────────────────
#define SMA_SIZE        10               // 10 x 50 ms = 500 ms window

// ── EMA filter ───────────────────────────────────────────────────────────
#define EMA_ALPHA       0.2f             // N_eff = (2-alpha)/alpha ~= 9 samples

// ── Noise/jitter comparison ─────────────────────────────────────────────
#define NOISE_WINDOW    40               // ~2 s worth of samples between STATS lines

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok = false;

// ── Calibration init (unchanged from module3.1) ─────────────────────────
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

    if (!s_cali_ok) ESP_LOGW(TAG, "No calibration -- mV columns will show 0");
}

// ── Filter 1: Simple Moving Average ─────────────────────────────────────
static int  s_sma_buf[SMA_SIZE];
static int  s_sma_idx   = 0;
static long s_sma_sum   = 0;
static int  s_sma_count = 0;

static int sma_update(int raw)
{
    s_sma_sum -= s_sma_buf[s_sma_idx];
    s_sma_buf[s_sma_idx] = raw;
    s_sma_sum += raw;
    s_sma_idx = (s_sma_idx + 1) % SMA_SIZE;
    if (s_sma_count < SMA_SIZE) s_sma_count++;
    return (int)(s_sma_sum / s_sma_count);
}

// ── Filter 2: Exponential Moving Average ────────────────────────────────
static float s_ema = -1.0f;   // -1 = "not initialized yet"

static float ema_update(int raw)
{
    if (s_ema < 0.0f) {
        s_ema = (float)raw;   // seed with the first real sample
    } else {
        s_ema += EMA_ALPHA * ((float)raw - s_ema);
    }
    return s_ema;
}

// ── Jitter (noise) accumulators, reset every NOISE_WINDOW samples ──────
static long  s_jit_raw_sum = 0, s_jit_sma_sum = 0;
static float s_jit_ema_sum = 0.0f;
static int   s_jit_count   = 0;
static int   s_prev_raw = 0, s_prev_sma = 0;
static float s_prev_ema = 0.0f;
static bool  s_have_prev = false;

// ── Entry point ──────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
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

    printf("\n");
    printf("+--------------------------+----------------------------------+\n");
    printf("| Parameter                | Value                            |\n");
    printf("+--------------------------+----------------------------------+\n");
    printf("| ADC unit / channel       | ADC1 / CH9  (GPIO10)              |\n");
    printf("| SMA window               | %d samples (%d ms)               |\n", SMA_SIZE, SMA_SIZE * READ_PERIOD_MS);
    printf("| EMA alpha (N_eff)        | %.2f  (~%.0f samples)             |\n", EMA_ALPHA, (2.0f - EMA_ALPHA) / EMA_ALPHA);
    printf("| Read interval            | %d ms                             |\n", READ_PERIOD_MS);
    printf("| STATS every              | %d samples (~%.1f s)              |\n", NOISE_WINDOW, NOISE_WINDOW * READ_PERIOD_MS / 1000.0f);
    printf("+--------------------------+----------------------------------+\n\n");

    printf("%-6s  %-6s  %-6s  %-6s  %-8s  %-8s  %-8s\n",
           "N", "RAW", "SMA", "EMA", "RAW_mV", "SMA_mV", "EMA_mV");
    printf("--------------------------------------------------------------\n");

    uint32_t n = 0;

    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));

        const int   sma = sma_update(raw);
        const float ema = ema_update(raw);
        const int   ema_i = (int)(ema + 0.5f);

        int raw_mv = 0, sma_mv = 0, ema_mv = 0;
        if (s_cali_ok) {
            adc_cali_raw_to_voltage(s_cali, raw, &raw_mv);
            adc_cali_raw_to_voltage(s_cali, sma, &sma_mv);
            adc_cali_raw_to_voltage(s_cali, ema_i, &ema_mv);
        }

        printf("%-6lu  %-6d  %-6d  %-6d  %-8d  %-8d  %-8d\n",
               (unsigned long)n, raw, sma, ema_i, raw_mv, sma_mv, ema_mv);

        // ── Jitter accumulation: mean |sample[k] - sample[k-1]| per signal ──
        if (s_have_prev) {
            s_jit_raw_sum += labs(raw - s_prev_raw);
            s_jit_sma_sum += labs(sma - s_prev_sma);
            s_jit_ema_sum += fabsf(ema - s_prev_ema);
            s_jit_count++;
        }
        s_prev_raw = raw;
        s_prev_sma = sma;
        s_prev_ema = ema;
        s_have_prev = true;

        if (s_jit_count >= NOISE_WINDOW) {
            const float raw_jit = (float)s_jit_raw_sum / s_jit_count;
            const float sma_jit = (float)s_jit_sma_sum / s_jit_count;
            const float ema_jit = s_jit_ema_sum / s_jit_count;

            ESP_LOGI(TAG, "STATS  mean|delta| raw=%.2f  sma=%.2f (-%.0f%%)  ema=%.2f (-%.0f%%)",
                     raw_jit, sma_jit,
                     raw_jit > 0 ? (1.0f - sma_jit / raw_jit) * 100.0f : 0.0f,
                     ema_jit,
                     raw_jit > 0 ? (1.0f - ema_jit / raw_jit) * 100.0f : 0.0f);

            s_jit_raw_sum = 0;
            s_jit_sma_sum = 0;
            s_jit_ema_sum = 0.0f;
            s_jit_count   = 0;
        }

        n++;
        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
