#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

static const char *TAG = "POT_PWM";

// ── ADC config ────────────────────────────────────────────────────────────────
#define ADC_UNIT        ADC_UNIT_2
#define ADC_CHANNEL     ADC_CHANNEL_0    // GPIO10 on ESP32-S3 → ADC1 channel 9
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12
#define ADC_MAX_RAW     4095
#define READ_PERIOD_MS  100

// ── LEDC (PWM) config — LED channel ─────────────────────────────────────────
#define LED_GPIO        GPIO_NUM_4
#define LED_TIMER       LEDC_TIMER_0
#define LED_CHANNEL     LEDC_CHANNEL_0
#define LED_FREQ_HZ     5000

// ── LEDC (PWM) config — motor channel ───────────────────────────────────────
#define MOTOR_GPIO       GPIO_NUM_8
#define MOTOR_TIMER      LEDC_TIMER_1
#define MOTOR_CHANNEL    LEDC_CHANNEL_1
#define MOTOR_FREQ_HZ    20000            // above audible range, motor-driver friendly

#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY    ((1 << 10) - 1)  // 1023

static adc_oneshot_unit_handle_t s_adc;

// ── LEDC init: two independent timers/channels, one per actuator ───────────
static void ledc_init(void)
{
    const ledc_timer_config_t led_timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LED_TIMER,
        .freq_hz         = LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&led_timer_cfg));

    const ledc_channel_config_t led_channel_cfg = {
        .gpio_num   = LED_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LED_CHANNEL,
        .timer_sel  = LED_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&led_channel_cfg));

    const ledc_timer_config_t motor_timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = MOTOR_TIMER,
        .freq_hz         = MOTOR_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&motor_timer_cfg));

    const ledc_channel_config_t motor_channel_cfg = {
        .gpio_num   = MOTOR_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = MOTOR_CHANNEL,
        .timer_sel  = MOTOR_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&motor_channel_cfg));

    ESP_LOGI(TAG, "LEDC ready: LED ch%d/timer%d @ %d Hz, motor ch%d/timer%d @ %d Hz",
             LED_CHANNEL, LED_TIMER, LED_FREQ_HZ,
             MOTOR_CHANNEL, MOTOR_TIMER, MOTOR_FREQ_HZ);
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
    printf("| LED PWM                  | GPIO4,  ch0/timer0, 5 kHz  |\n");
    printf("| Motor PWM                | GPIO5,  ch1/timer1, 20 kHz |\n");
    printf("| PWM resolution           | 10-bit  (0 - 1023)         |\n");
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

    ledc_init();

    // ── Table header ─────────────────────────────────────────────────────────
    printf("%-6s  %-10s\n", "RAW", "DUTY");
    printf("--------------------\n");

    // ── Main control loop ────────────────────────────────────────────────────
    while (true) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, ADC_CHANNEL, &raw));

        // 12-bit ADC range → 10-bit PWM duty range
        const uint32_t duty = (uint32_t)raw * LEDC_MAX_DUTY / ADC_MAX_RAW;

        // LED and motor are driven independently — separate timers, channels
        // and GPIOs, so one cannot influence the other's PWM signal.
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LED_CHANNEL, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LED_CHANNEL));

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, MOTOR_CHANNEL, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, MOTOR_CHANNEL));

        printf("%-6d  %-10lu\n", raw, (unsigned long)duty);

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
