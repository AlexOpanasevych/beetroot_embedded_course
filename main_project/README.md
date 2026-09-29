# Main Project — Desk Guardian: Networked Sentry

A capstone that extends the **miniproject_module5** "Desk Guardian" PIR/buzzer/LED
board into a small networked environmental sentry: it still barks at motion, but
now also reads temperature/humidity/pressure, spins a PID-controlled cooling fan,
and serves a live status dashboard over Wi-Fi — no PC/serial monitor required to
see what it's doing.

Hardware is **Desk Guardian PCB rev B** (`pcb/`): the miniproject_module5 board
with the PIR/buzzer/LED circuitry unchanged, plus an on-board BME280 header and
a fan low-side driver for the new features — see [PCB — rev B](#pcb--rev-b).

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
| Fan driver circuit (Q2/D2/R11) | miniproject_module5's buzzer driver (Q1/D1/R6) | Same MMBT3904 + 1N4148 topology, switched to +5V, plus a base pull-down and bulk cap |

## Wiring

Carried over unchanged from miniproject_module5:

| Signal | ESP32-S3 pin | Notes |
|---|---|---|
| PIR_IN | IO4 | RC-filtered on the board, R5 already pulls it down when idle |
| Buzzer control | IO5 | via the board's existing NPN transistor driver |
| LED eye 1 | IO6 | |
| LED eye 2 | IO7 | |

New on rev B (were no-connect pins on rev A):

| Signal | ESP32-S3 pin | On-board | Notes |
|---|---|---|---|
| BME280 SDA | IO8 (U2 pad 12) | J3 pin 4, R9 4.7k pull-up, TP1 | |
| BME280 SCL | IO9 (U2 pad 17) | J3 pin 3, R10 4.7k pull-up, TP2 | |
| BME280 VIN / GND | 3V3 / GND | J3 pins 1 / 2, C6 100nF | GY-BME280 pin order (VIN GND SCL SDA); module's SDO tied to GND -> address 0x76 |
| Fan PWM | IO18 (U2 pad 11) | R11 1k -> Q2 base, TP3 | R12 10k holds the fan off while IO18 floats during boot |
| Fan | +5V / FAN_DRV | J4 pins 1 / 2, D2 flyback, C7 10uF, TP4 | 5V fan, **<= 150 mA** (MMBT3904 is rated 200 mA) |

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

`aggregator_task` is the *only* reader of the shared queue and the only writer
of the sensor/motion fields of `system_state_t` — exactly the module6.3
invariant ("no shared mutable state between producers; the queue itself is the
synchronization"). The few status fields that don't come from sensors (fan
duty, Wi-Fi state, CPU load) are written by their owners through
`sensor_hub_set_*()`, under the same mutex. `pir_task` reacts to motion
immediately and independently of the queue (the buzzer/LEDs can't wait on a
consumer task), then reports the event into the same queue afterwards so the
dashboard picks it up too.

A fourth task, `cpu_monitor_task` (every 3s), measures CPU load and
critical-section timing — see the Performance sections below.

## Reliability

`aggregator_task`, `pir_task`, `bme280_task`, and `cpu_monitor_task` are each
registered with ESP-IDF's task watchdog (`esp_task_wdt_add`) and reset it
(`esp_task_wdt_reset`) at least every ~3s — including on `pir_task`'s idle
poll and inside its post-motion cooldown (which is a menuconfig value and,
together with the bark, already exceeds 5s at the default, so it sleeps in 1s
slices). A task that truly hangs (stuck I2C transaction, deadlocked mutex,
etc.) trips the 5s watchdog and reboots the board (`CONFIG_ESP_TASK_WDT_PANIC=y`)
instead of silently freezing the sentry, while legitimate quiet periods never
trip it.

## Performance — CPU load monitoring

`cpu_monitor.cpp` samples FreeRTOS run-time stats every 3s
(`configGENERATE_RUN_TIME_STATS`, `ulTaskGetIdleRunTimeCounter()`) and
computes load as a rolling window (delta between samples), not a
since-boot average, so it reflects current conditions rather than slowly
drifting as uptime grows. Logged every sample (`ESP_LOGW` once load reaches
the 70% budget, `ESP_LOGI` otherwise) and pushed to the dashboard's
`cpu_load_pct` field. Not yet measured on hardware; the busiest paths should
be the 2s BME280/PID cycle, dashboard requests, and Wi-Fi, so load is expected
to stay far below the 70% budget — the log will confirm or refute that.

## Performance — critical-section timing

`crit_timing.h/.cpp` is a small min/max/avg microsecond recorder
(`esp_timer_get_time()`-based) used to actually measure, not just assume,
how long this project's two real critical sections stay held:

- **`sensor_hub`'s state mutex** — every `sensor_hub_get_state()` /
  `_set_fan_duty()` / `_set_wifi()` / `_set_cpu_load()` call and
  `aggregator_task`'s own update time the span between a successful
  `xSemaphoreTake` and the matching `xSemaphoreGive` (wait-to-acquire time
  is deliberately excluded — that's contention, not hold time).
- **The PIR GPIO ISR** (`pir_isr_handler`) — timed end-to-end, since the
  whole handler body is the "critical section" there. `crit_timing_record()`
  is `IRAM_ATTR` (pure integer math) so it's safe to call from an ISR that's
  itself `IRAM_ATTR`.

The stats themselves are shared data too, so they're guarded: the mutex
stats are only touched while holding the state mutex, and the ISR stats sit
behind a `portMUX` spinlock shared by the ISR and the reporting task.

Both get logged (and their stats reset) every 3s alongside the CPU load
report, by `cpu_monitor.cpp`. Expect low-single-digit microseconds for the
mutex (it only ever guards a small struct copy/field write) and a similar
order of magnitude for the ISR (it just gives a semaphore) — if either
climbs into the hundreds of microseconds or higher, that's a real regression
worth investigating, not noise.

## PCB — rev B

KiCad 10 project in `pcb/desk_guardian_sentry.*`, derived from
miniproject_module5's `desk_guardian` board (the original there is left
untouched as the rev A submission).

- **Board:** 2-layer, 78 x 106 mm, ESP32-S3-WROOM-1 soldered directly
  (antenna overhanging the top edge), USB-C 5V in -> AMS1117-3.3 LDO.
- **Rev B additions** (lower-right area, previously empty): J3 BME280 header,
  R9/R10 I2C pull-ups, C6 decoupling; Q2/D2/R11/R12/C7 fan driver, J4 fan
  header; test points TP1 SDA, TP2 SCL, TP3 FAN_PWM, TP4 FAN_DRV, TP5 GND.
  Both headers sit on the right board edge for off-board cabling.
- **Power / filtering:** C6 100nF right at the BME280 header's 3V3/GND pins;
  C7 10uF bulk on +5V next to the fan header, to absorb motor current spikes
  locally instead of pulling them back through the whole +5V run. D2 clamps
  the fan's inductive kick when Q2 switches off.
- **Signal / power separation:** F.Cu is a solid GND pour, so the new routing
  stays on B.Cu wherever possible — 310 of 367 mm (84%); the F.Cu pieces are
  short pad escapes plus the 15 mm local FAN_BASE net between R11/R12/Q2.
  Power runs are wider (+5V / FAN_DRV 0.6 mm, +3V3 0.4 mm) than signals
  (0.25 mm). Fan current reaches J4 on its own 0.6 mm +5V branch from the
  USB input rather than through the LDO's traces (~11 mV drop at 150 mA).
- **High-speed considerations:** nothing on this board is fast — I2C runs at
  100 kHz and the fan PWM at 5 kHz, so trace length and impedance matching
  aren't critical. The one RF-sensitive part, the WROOM antenna, has no
  copper routed under or near it.
- **Verification:** ERC 0 errors; DRC 0 errors, 0 unconnected items, 0
  schematic-parity issues. The only warnings are ones rev A already had
  (library-version drift on a few symbols, U2's silkscreen clipped by the
  board edge because the antenna overhangs).

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

- Wi-Fi is STA-only with no fallback: `wifi_sta_init()` waits up to 15s for
  the initial connection, then boots the rest of the system (PIR sentry,
  fan PID) regardless — the dashboard just stays unreachable until a
  connection succeeds in the background (watch the serial log for
  "No Wi-Fi after..." vs. "Got IP: ...").
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
