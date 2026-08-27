# Module 4.5 — ESP32 receiver for the STM32 environment telemetry protocol

SPI2 master (ESP-IDF via PlatformIO) that polls the STM32F401 slave from module 4.4
every 300 ms, decodes the custom telemetry frame, and pretty-prints the parsed fields
to the serial monitor.

This is the same SPI wiring/timing as the module 4.3 mini-project
(`beetroot_embedded_course/miniproject_module4`): PA4/PA5/PA6/PA7 on the STM32 side,
GPIO10 (CS) / GPIO11 (MOSI) / GPIO12 (SCLK) / GPIO13 (MISO) on the ESP32 side, 200 kHz,
SPI mode 0.

## Protocol

See the frame-layout comment block near the top of `src/main.cpp` (kept in sync with
the same comment in `module4.4/Core/Src/main.c`). Summary:

- 20-byte fixed frame, byte 0 always a dummy (hardware-NSS slave quirk).
- Request: sync byte + command byte + checksum, sent every poll.
- Response (one transaction behind, since the STM32 preloads it): ACK byte, date,
  time, temperature ×100, humidity ×100, pressure ×10, light %, a status byte, checksum.

`parse_response()` validates the ACK byte and the XOR checksum before trusting a
frame; `build_request()` mirrors it on the way out.

## Example output

```
I (12345) spi_env_telemetry:
+-------------------------------------------+
|            STM32 ENVIRONMENT DATA          |
+-------------------------------------------+
| Date/Time  : 2026-08-20  12:03:47          |
| Temperature:  24.37 C                      |
| Humidity   :  41.82 %RH                    |
| Pressure   :  1008.6 hPa                    |
| Light level:  67 %                         |
| Sensors    : BME280=OK   RTC=OK            |
+-------------------------------------------+
```

## Build

```
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor
```
