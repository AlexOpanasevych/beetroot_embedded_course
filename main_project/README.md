# Main Project — Desk Guardian: Networked Sentry

A capstone that extends the **miniproject_module5** "Desk Guardian" PIR/buzzer/LED
board into a small networked environmental sentry: it still barks at motion, but
now also reads temperature/humidity/pressure, spins a PID-controlled cooling fan,
and serves a live status dashboard over Wi-Fi — no PC/serial monitor required to
see what it's doing.

This is deliberately **firmware-only** on top of the existing Desk Guardian
hardware: the PIR/buzzer/LED wiring is unchanged, and the BME280 + fan are added
on breadboard/jumper wires rather than a new PCB revision.

## What it reuses, and from where

| Piece | Reused from | What changed |
|---|---|---|
| I2C driver style (`driver/i2c_master.h`) | module4.2 (SSD1306 ticker) | Same bus-init pattern, new device (BME280) instead of an OLED |
| BME280 register map + Bosch compensation math | module4.4 (STM32 BME280 telemetry) | Ported from STM32 HAL calls to ESP-IDF's `i2c_master_transmit`/`_transmit_receive` |
| Multi-producer -> single-consumer FreeRTOS queue | module6.3 | Two producers (BME280 poll task, PIR task) instead of three; one aggregator task owns all shared state |
| ISR + binary semaphore, 0% CPU while idle | module6.5 (emergency-stop task) | Applied to the PIR input instead of a panic button |
| PID fan controller (incl. the D-sign fix, anti-windup, and min-duty output normalization) | module5.6 | Same math, fed by BME280 temperature instead of a thermistor divider; PID step runs whenever a fresh reading arrives instead of on a fixed 50 ms timer |
| Wi-Fi STA connect (event group, auto-reconnect) | miniproject_module3 (Wi-Fi subwoofer amp) | Same connect logic, serving a status page instead of streaming audio |
| Buzzer/LED "bark" cadence | module1.3 (flash/gap blink constants) | Same 80 ms/60 ms flash/gap timing, applied to the buzzer+LED alert instead of a plain blink |
| PIR + buzzer + LED-eyes hardware | miniproject_module5 (Desk Guardian PCB) | Unchanged — same pins, same RC filtering |

## Wiring

Desk Guardian PCB (unchanged from miniproject_module5):

| Signal | ESP32-S3 pin | Notes |
|---|---|---|
| PIR_IN | IO4 | RC-filtered on the board, R5 already pulls it down when idle |
| Buzzer control | IO5 | via the board's existing NPN transistor driver |
| LED eye 1 | IO6 | |
| LED eye 2 | IO7 | |

New, on breadboard:

| Signal | ESP32-S3 pin | Notes |
|---|---|---|
| BME280 SDA | IO8 | same pin as module4.2's OLED, free on the Desk Guardian board |
| BME280 SCL | IO9 | |
| BME280 VDD / GND | 3V3 / GND | SDO tied to GND -> I2C address 0x76 |
| Fan PWM | IO18 | through an NPN transistor + flyback diode, same technique as the board's buzzer driver — build this on breadboard |

## Architecture

```
                 ISR (rising edge)
   PIR (IO4) ────────────────────► motion semaphore ──► pir_task
                                                            │
                                                     bark (buzzer+LEDs,
                                                      module1.3 cadence)
                                                            │
                                                            ▼
   BME280 (I2C) ──► bme280_task ──────────────────►  sensor_hub queue
      (every 2s)                                    (module6.3 pattern)
                                                            │
                                                            ▼
                                                    aggregator_task
                                                  (single consumer,
                                                 owns system_state_t)
                                                     │           │
                                          fan_pid_on_new_        │
                                          temperature()          │
                                                     │           │
                                                     ▼           ▼
                                              LEDC fan PWM   sensor_hub_get_state()
                                               (IO18)              │
                                                                   ▼
                                                          web_dashboard (/, /status.json)
                                                             served over Wi-Fi STA
```

`aggregator_task` is the *only* task that ever writes `system_state_t` (guarded
by a mutex), and the *only* reader of the shared queue — exactly the module6.3
invariant ("no shared mutable state between producers; the queue itself is the
synchronization"). `pir_task` reacts to motion immediately and independently of
the queue (the buzzer/LEDs can't wait on a consumer task), then reports the
event into the same queue afterwards so the dashboard picks it up too.

## Build & run

```
pio run -t menuconfig
# -> "Desk Guardian Sentry Configuration": set your Wi-Fi SSID/password,
#    the fan's PID setpoint temperature, and the motion cooldown.

pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -b 115200   # watch for "Got IP: ..."
```

Then open `http://<that-ip>/` in a browser on the same LAN. The page polls
`/status.json` every 2 seconds; a motion event turns the "Motion" card red
until the next PIR cooldown window ends.

## Known limitations

- Wi-Fi is STA-only with no fallback — like miniproject_module3's audio
  streamer, wrong credentials mean `wifi_sta_init()` blocks forever waiting
  to connect (watch the serial log if the dashboard never comes up).
- No wall-clock timestamps: `last_motion_ms`/`bme280_updated_ms` are
  milliseconds since boot (`xTaskGetTickCount()`), not real time — the board
  has no RTC and this project doesn't add SNTP, so treat them as relative
  ("Xs ago"), not absolute.
- The dashboard has no auth — anyone on the LAN can view it (and see when
  you're not at your desk). Fine for a demo, not for anything beyond a
  trusted home/lab network, same caveat as the subwoofer amp's UDP stream.
- PID coefficients are the module5.6 starting values, carried over as-is;
  the BME280's thermal mass/response in open air will differ from the
  thermistor-on-a-divider setup they were tuned for, so expect to retune
  Kp/Ki/Kd (watch the `SP/PV/OUT/PWM` log lines) once real hardware is wired up.
