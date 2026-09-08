# Miniproject module 4 — SPI-controlled remote LED with an I2C status display

ESP32-S3 acting as master on two independent buses at once:

- **SPI2** — a framed command/ACK link to an STM32F401 slave (`stm32slave/`), toggling
  its PB0 LED on/off every 300 ms and reading back an ACK.
- **I2C0** — drives an SSD1306 128x64 OLED that mirrors the link's live state (commanded
  LED value, link OK/FAIL, running transaction/error counters), so the state of the
  SPI link is visible without a serial monitor and without adding anything to the
  STM32 side.

## Wiring

SPI (ESP32 <-> STM32, SPI mode 0, 200 kHz):

| Signal | ESP32-S3 | STM32F401 (SPI1) |
|--------|----------|-------------------|
| SCLK   | GPIO12   | PA5               |
| MOSI   | GPIO11   | PA7               |
| MISO   | GPIO13   | PA6               |
| CS/NSS | GPIO10   | PA4               |

I2C (ESP32 -> SSD1306 OLED):

| Signal | ESP32-S3 |
|--------|----------|
| SDA    | GPIO8    |
| SCL    | GPIO9    |

## Protocol

8-byte fixed frame, byte 0 always a dummy (hardware-NSS slave quirk — see the comment
in `stm32slave/Src/main.c`'s `Process_SPI_Command`).

- Request: `[dummy, 0xA5, CMD_SET_LED, arg, checksum, ...]`
- Response: `[dummy, 0x5A, cmd echo, arg applied, status, checksum, ...]`

Checksum is the XOR of the payload bytes; either side drops a frame whose checksum or
sync/ack marker doesn't match.

## Build

```
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor
```

The STM32 side (`stm32slave/`) is a separate STM32CubeIDE project — build/flash it
independently via CubeIDE or `arm-none-eabi-gcc`/`st-flash`.
