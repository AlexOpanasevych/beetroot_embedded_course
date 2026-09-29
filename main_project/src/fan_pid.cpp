#include "fan_pid.h"
#include "sensor_hub.h"

#include <stdio.h>
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "FAN_PID";

#define MOTOR_PWM_GPIO    GPIO_NUM_18   // matches module5.6's fan PWM pin
#define MOTOR_PWM_FREQ_HZ 5000
#define MOTOR_RESOLUTION  LEDC_TIMER_10_BIT
#define MAX_DUTY_CYCLE    1023
#define MOTOR_MIN_PWM     200 // smallest duty at which the fan still reliably spins

typedef struct {
    float Kp;
    float Ki;
    float Kd;
    float integral;
    float prev_pv;
    float out_min;
    float out_max;
} pid_controller_t;

// Same starting coefficients as module5.6 — retune Kp/Ki/Kd on real hardware,
// the thermal mass/response here (BME280 in open air vs. a thermistor divider)
// won't be identical.
static pid_controller_t s_fan_pid = {
    .Kp = 60.0f,
    .Ki = 8.0f,
    .Kd = 25.0f,
    .integral = 0.0f,
    .prev_pv = 0.0f,
    .out_min = 0.0f,
    .out_max = (float)MAX_DUTY_CYCLE,
};

static const float SETPOINT_TEMP = (float)CONFIG_FAN_SETPOINT_TEMP_C;
static int64_t s_last_time_us = 0;
static bool s_first_sample = true;

void fan_pid_init(void)
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = MOTOR_RESOLUTION;
    timer_cfg.timer_num = LEDC_TIMER_0;
    timer_cfg.freq_hz = MOTOR_PWM_FREQ_HZ;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num = MOTOR_PWM_GPIO;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = LEDC_CHANNEL_0;
    ch_cfg.timer_sel = LEDC_TIMER_0;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

// Linearly maps the continuous PID output [0, out_max] onto the fan's real
// working PWM range [MOTOR_MIN_PWM, MAX_DUTY_CYCLE] instead of a hard
// threshold, so there's no discontinuity right as the fan starts spinning.
static uint32_t normalize_pid_output_to_duty(float output, float out_max)
{
    if (output <= 0.0f) {
        return 0;
    }

    float normalized = MOTOR_MIN_PWM + (output / out_max) * (float)(MAX_DUTY_CYCLE - MOTOR_MIN_PWM);

    if (normalized > (float)MAX_DUTY_CYCLE) normalized = (float)MAX_DUTY_CYCLE;
    if (normalized < (float)MOTOR_MIN_PWM) normalized = (float)MOTOR_MIN_PWM;

    return (uint32_t)normalized;
}

void fan_pid_on_new_temperature(float temperature_c)
{
    int64_t now = esp_timer_get_time();

    if (s_first_sample) {
        s_fan_pid.prev_pv = temperature_c;
        s_last_time_us = now;
        s_first_sample = false;
        return;
    }

    float dt = (float)(now - s_last_time_us) / 1000000.0f;
    s_last_time_us = now;
    if (dt <= 0.00001f) dt = 1.0f;

    float error = temperature_c - SETPOINT_TEMP;

    // error(SP fixed) derivative == PV derivative; positive while warming up
    // -> D term leads the response instead of fighting it (module5.6's fix).
    float derivative = (temperature_c - s_fan_pid.prev_pv) / dt;
    s_fan_pid.prev_pv = temperature_c;

    float output_pre = (s_fan_pid.Kp * error) + (s_fan_pid.Ki * s_fan_pid.integral) + (s_fan_pid.Kd * derivative);

    // Conditional integration (anti-windup): don't accumulate the integral
    // if the output is already saturated in the direction the error is pushing it.
    if ((output_pre < s_fan_pid.out_max || error < 0.0f) &&
        (output_pre > s_fan_pid.out_min || error > 0.0f)) {
        s_fan_pid.integral += error * dt;
    }

    if (s_fan_pid.Ki > 0.0f) {
        float integral_limit = s_fan_pid.out_max / s_fan_pid.Ki;
        if (s_fan_pid.integral > integral_limit) s_fan_pid.integral = integral_limit;
        if (s_fan_pid.integral < -integral_limit) s_fan_pid.integral = -integral_limit;
    }

    float output = (s_fan_pid.Kp * error) + (s_fan_pid.Ki * s_fan_pid.integral) + (s_fan_pid.Kd * derivative);
    if (output > s_fan_pid.out_max) output = s_fan_pid.out_max;
    if (output < s_fan_pid.out_min) output = s_fan_pid.out_min;

    uint32_t duty = normalize_pid_output_to_duty(output, s_fan_pid.out_max);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    sensor_hub_set_fan_duty(duty, MAX_DUTY_CYCLE);

    ESP_LOGI(TAG, "SP:%.1f PV:%.2f OUT:%.1f PWM:%lu (%.0f%%)",
             SETPOINT_TEMP, temperature_c, output, (unsigned long)duty,
             100.0f * duty / MAX_DUTY_CYCLE);
}
