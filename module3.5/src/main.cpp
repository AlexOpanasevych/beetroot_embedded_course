/*
 * Module 3.5 — Potentiometer-Controlled Servo (1:1 angle mapping)
 *
 * Board       : ESP32-S3-DevKitC-1
 * Potentiometer: wiper -> GPIO1  (ADC1 channel 0)
 * Servo       : signal -> GPIO18 (LEDC PWM, 50 Hz)
 *
 * ── Task ─────────────────────────────────────────────────────────────────
 *   The potentiometer drives the servo shaft in a 1:1 angle ratio: turning
 *   the pot by X degrees moves the servo by the same X degrees. The pot's
 *   mechanical sweep (POT_MIN_DEG..POT_MAX_DEG) and the servo's mechanical
 *   sweep (SERVO_MIN_DEG..SERVO_MAX_DEG) generally don't match — only the
 *   overlapping part of the two ranges is usable, so any pot angle outside
 *   that intersection is clipped to the nearest edge of the servo's range.
 *   The resulting angle, measured from the servo's leftmost (0 deg)
 *   position, is logged to the console.
 *
 * ── Adjusting the ranges ─────────────────────────────────────────────────
 *   POT_MIN_DEG / POT_MAX_DEG     — potentiometer's physical rotation limits
 *   SERVO_MIN_DEG / SERVO_MAX_DEG — servo's physical rotation limits
 *   The overlap is computed at compile time from those four values.
 */

#include <stdio.h>
#include <algorithm>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

static const char *TAG = "POT_SERVO";

// ── ADC (potentiometer) config ──────────────────────────────────────────
#define ADC_UNIT        ADC_UNIT_2
#define ADC_CHANNEL     ADC_CHANNEL_0     // GPIO11
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12
#define ADC_MAX_RAW     4095
#define READ_PERIOD_MS  100

// ── Mechanical ranges (degrees) ─────────────────────────────────────────
#define POT_MIN_DEG     0.0f
#define POT_MAX_DEG     300.0f   // typical rotary-potentiometer mechanical sweep
#define SERVO_MIN_DEG   0.0f
#define SERVO_MAX_DEG   180.0f   // typical hobby-servo mechanical sweep

// Intersection of [POT_MIN_DEG, POT_MAX_DEG] and [SERVO_MIN_DEG, SERVO_MAX_DEG]
// — the only part of the pot's sweep that has a matching servo angle.
static constexpr float OVERLAP_MIN_DEG = std::max(POT_MIN_DEG, SERVO_MIN_DEG);
static constexpr float OVERLAP_MAX_DEG = std::min(POT_MAX_DEG, SERVO_MAX_DEG);

// ── Servo PWM (LEDC) config ─────────────────────────────────────────────
#define SERVO_GPIO       GPIO_NUM_18
#define SERVO_TIMER      LEDC_TIMER_0
#define SERVO_CHANNEL    LEDC_CHANNEL_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES    LEDC_TIMER_14_BIT
#define LEDC_MAX_DUTY    ((1 << 14) - 1)  // 16383
#define SERVO_FREQ_HZ    50                // 50 Hz (20 ms period)
#define SERVO_PERIOD_US  (1000000 / SERVO_FREQ_HZ)   // 20000 us
#define SERVO_MIN_PULSE_US  500   // pulse width at SERVO_MIN_DEG
#define SERVO_MAX_PULSE_US  2500  // pulse width at SERVO_MAX_DEG

static adc_oneshot_unit_handle_t s_adc;

// ── Init helpers ─────────────────────────────────────────────────────────

static void adc_init(void)
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
}

static void servo_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = SERVO_TIMER,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t channel_cfg = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

// Drive the servo to `angle_deg`, measured from SERVO_MIN_DEG (leftmost).
static void servo_set_angle(float angle_deg)
{
    const float pulse_us = SERVO_MIN_PULSE_US +
        (angle_deg - SERVO_MIN_DEG) / (SERVO_MAX_DEG - SERVO_MIN_DEG) *
        (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US);

    const uint32_t duty = (uint32_t)(pulse_us * LEDC_MAX_DUTY / SERVO_PERIOD_US);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, SERVO_CHANNEL));
}

// ── Entry point ───────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    adc_init();
    servo_init();

    printf("\n");
    printf("+--------------------------+----------------------------------+\n");
    printf("| Parameter                | Value                            |\n");
    printf("+--------------------------+----------------------------------+\n");
    printf("| Potentiometer sweep      | %5.1f - %5.1f deg               |\n", POT_MIN_DEG, POT_MAX_DEG);
    printf("| Servo sweep              | %5.1f - %5.1f deg               |\n", SERVO_MIN_DEG, SERVO_MAX_DEG);
    printf("| Usable (overlap) range   | %5.1f - %5.1f deg               |\n", OVERLAP_MIN_DEG, OVERLAP_MAX_DEG);
    printf("| Servo GPIO / PWM         | GPIO%d, %d Hz                    |\n", SERVO_GPIO, SERVO_FREQ_HZ);
    printf("+--------------------------+----------------------------------+\n\n");

    float last_logged_deg = -1.0f;   // force first log

    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));

        // Raw ADC counts -> potentiometer angle (1:1 with its own mechanical sweep)
        const float pot_deg = POT_MIN_DEG +
            (float)raw / ADC_MAX_RAW * (POT_MAX_DEG - POT_MIN_DEG);

        // Clip to the range shared by both the pot and the servo.
        const float servo_deg = std::clamp(pot_deg, OVERLAP_MIN_DEG, OVERLAP_MAX_DEG);

        servo_set_angle(servo_deg);

        // Log only on a meaningful (>= 1 deg) change to keep the console readable.
        if (fabsf(servo_deg - last_logged_deg) >= 1.0f) {
            last_logged_deg = servo_deg;
            ESP_LOGI(TAG, "raw=%4d  pot=%6.1f deg  servo angle (from left)=%6.1f deg",
                     raw, pot_deg, servo_deg);
        }

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
