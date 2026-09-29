/*
 * Fan speed PID loop, ported from module5.6's thermistor+PID fan controller.
 * Same controller math (including the D-sign and anti-windup fixes from
 * that module and the min-duty output normalization), just fed by BME280
 * temperature readings instead of a thermistor ADC channel.
 */
#pragma once

// Configures the LEDC timer/channel driving the fan MOSFET/transistor gate.
void fan_pid_init(void);

// Call whenever a fresh temperature reading is available (from aggregator_task).
// Internally measures its own dt via esp_timer, so it works correctly no
// matter how often the BME280 actually produces a new sample.
void fan_pid_on_new_temperature(float temperature_c);
