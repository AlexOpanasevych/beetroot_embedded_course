# Module 4.4 — STM32 BME280 + custom SPI telemetry protocol

Extends the module 4.3 mini-project (`beetroot_embedded_course/miniproject_module4/stm32slave`,
STM32F401 as an SPI1 hardware-NSS **slave**) with:

- **BME280** over **I2C1** — temperature, humidity, pressure (`bme280.c` / `bme280.h`)
- a **light sensor** (LDR voltage divider) on **ADC1_IN1 (PA1)** — reported as 0-100 %
- the **RTC** peripheral (LSI-clocked) — date/time

All five values are packed into a custom fixed-length frame and shipped to the ESP32
master (module 4.5) on every SPI transaction. The wire format is documented in detail
in the comment block above `Build_Env_Response()` in `Core/Src/main.c`, and the ESP32
side (`module4.5/src/main.cpp`) decodes the exact same layout.

## Wiring (in addition to the existing SPI1 link to the ESP32)

| Signal        | STM32F401 pin | Notes                                   |
|---------------|---------------|------------------------------------------|
| BME280 SCL    | PB6           | I2C1_SCL, open-drain + pull-up          |
| BME280 SDA    | PB7           | I2C1_SDA, open-drain + pull-up          |
| BME280 VDD/GND| 3.3V / GND    | BME280 SDO tied to GND -> I2C addr 0x76 |
| Light sensor  | PA1           | ADC1_IN1, LDR-on-top voltage divider    |
| SPI1 (to ESP32)| PA4/PA5/PA6/PA7 | unchanged from module 4.3             |
| Heartbeat LED | PB0           | unchanged from module 4.3 (toggles each serviced SPI transaction) |

## CubeMX changes needed on top of `stm32slave.ioc`

The `.ioc` in this repo only enables SPI1 + GPIO; regenerating code for this exercise
requires adding, in STM32CubeMX:

1. **I2C1**: Mode = I2C, default timing (100 kHz) — pins PB6/PB7 auto-assigned.
2. **ADC1**: enable channel **IN1** on PA1, resolution 12 bit, independent mode.
3. **RTC**: enable **Activate Clock Source**, Calendar; under *Clock Configuration* set
   **RTC clock source = LSI** (no external 32.768 kHz crystal assumed on this board).

Regenerate, then drop in the files from `Core/Inc` and `Core/Src` in this folder
(they follow the same `USER CODE BEGIN/END` structure CubeMX expects, and
`Core/Src/main.c`/`stm32f4xx_hal_msp.c` here already contain the hand-written
Init/Msp functions for I2C1/ADC1/RTC — merge them into the regenerated files rather
than overwriting CubeMX's `SPI1`/`GPIO` sections).

## Protocol summary

- Frame length: 20 bytes, byte 0 always a dummy (documented hardware-NSS slave quirk
  carried over from module 4.3 — the very first shifted byte isn't reliably latched).
- Request (ESP32 → STM32): sync byte, command byte, checksum.
- Response (STM32 → ESP32): ACK byte, date (Y/M/D), time (H/M/S), temperature ×100,
  humidity ×100, pressure ×10, light %, a status byte (BME280/RTC health), checksum.
- Every multi-byte numeric field is big-endian; checksum is XOR of the payload bytes.

See `module4.5/README.md` for the ESP32 side and a worked example of decoding one frame.
