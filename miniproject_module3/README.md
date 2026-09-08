# Wi-Fi Subwoofer Amp (ESP32-S3)

Two 15 W subwoofers, driven from a battery, with audio streamed from a PC/phone
over the LAN and played out through the ESP32-S3.

## Why Wi-Fi and not Bluetooth

The original ask was Bluetooth audio, but the ESP32-S3 can't do that:

- **A2DP** (standard "Bluetooth speaker" audio) is a classic-BT (BR/EDR) profile.
  The S3's radio is BLE-only — no classic BT hardware at all.
- **LE Audio / LC3** (the BLE-based audio profile) also doesn't run on the S3:
  Espressif's own docs state ISO channels — required for any LE Audio stream —
  are unimplemented at both the controller and host level on this chip, and
  Espressif has no shipping chip with LE Audio support yet.
  ([ESP-BLE-ISO docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/bluetooth/esp-ble-iso.html),
  [espressif/esp-idf#12277](https://github.com/espressif/esp-idf/issues/12277))

So this project streams raw PCM audio over **Wi-Fi/UDP** instead — the ESP32-S3
joins your LAN, and `tools/pc_audio_sender.py` captures your PC's system audio
and sends it straight to the board.

## Signal path

```
PC (system audio)                              Battery pack
      |                                              |
      | UDP/PCM over Wi-Fi                           v
      v                                     Boost converter (7.4V -> 12V)
ESP32-S3-DevKitC-1                                    |
  - Wi-Fi STA, UDP socket                    +--------+--------+
  - software volume (pot via ADC)            |                 |
  - I2S TX (BCLK/WS/DOUT)                    v                 v
      |                                 TPA3116D2 amp    Buck 12V->5V
      v                                 board (2x)             |
PCM5102A I2S DAC  --- analog L/R --->    L in    R in     ESP32-S3 5V,
  (I2S in, stereo                         |        |      PCM5102 VCC
   line-out)                              v        v
                                     Subwoofer 1  Subwoofer 2
                                      (15 W, 4ohm) (15 W, 4ohm)
```

- ESP32-S3 talks **I2S digital audio** to a PCM5102A DAC breakout, which
  outputs analog stereo line-level audio.
- That feeds a TPA3116D2 (or TPA3110D2) class-D amp board, one channel per
  subwoofer, each rated for ~15 W into 4 ohm at this supply voltage.
- The amp's mute/enable pin is wired to an ESP32-S3 GPIO so it stays silent
  until Wi-Fi is connected and the audio pipeline is actually running.

## Battery / power assumption — please double-check

"4.7V batteries" isn't a standard cell voltage, so I'm assuming a typo for
the very standard **3.7V Li-ion cell**. Scheme used here:

- **2x 18650 Li-ion cells in series (2S)**, nominal 7.4V / full charge 8.4V,
  behind a 2S BMS (over-discharge, overcurrent, balance protection) and
  charged with a TP5100-style 2S charger module.
- A boost converter (e.g. MT3608, set to 12V) steps the pack up to a fixed
  12V rail for the TPA3116D2 board — this is what actually gives you ~15W/ch
  into 4-ohm subs.
- A small buck regulator (12V -> 5V) powers the ESP32-S3 5V pin and the
  PCM5102 DAC logic.

If you actually meant something else (e.g. a single-cell 3.7V pack, or a
specific pre-built battery you already have), say so and I'll rework the
power stage — it changes the boost/buck sizing and possibly the amp choice.

## Pin map (ESP32-S3-DevKitC-1)

| Signal              | GPIO | Notes                                   |
|---------------------|------|------------------------------------------|
| I2S BCLK            | 4    | to PCM5102 BCK                            |
| I2S WS (LRCLK)       | 5    | to PCM5102 LCK                            |
| I2S DOUT            | 6    | to PCM5102 DIN                            |
| Status LED          | 2    | solid = streaming, slow blink = idle/wait |
| Amp enable/mute     | 7    | drives TPA3116D2 mute/SD pin (active-high enable) |
| Volume pot (ADC1_CH0)| 1   | wiper here, ends to 3V3/GND                |

PCM5102 `SCK` pin: tie to GND (use its internal PLL, no MCLK line needed).

## Firmware config

Wi-Fi credentials and the UDP port are **not** hardcoded — set them via:

```
pio run -t menuconfig
# -> "Subwoofer Wi-Fi Audio Configuration"
```

or edit `sdkconfig.esp32-s3-devkitc-1` directly for `CONFIG_WIFI_SSID`,
`CONFIG_WIFI_PASSWORD`, `CONFIG_AUDIO_UDP_PORT` after a first menuconfig run.

## Running it

1. Flash: `pio run -t upload`
2. Monitor: `pio device monitor -b 115200` — watch for "Got IP: ..."
3. On the PC: `pip install soundcard numpy`, then
   `python tools/pc_audio_sender.py <esp32-ip-from-log>`
4. Play any audio on the PC — it streams to the ESP32 and out through the amp.

## Known limitations (mini-project scope)

- No jitter buffer or packet-loss recovery — this is a raw point-to-point PCM
  link, fine on a quiet LAN, not resilient to Wi-Fi congestion.
- No auth/encryption on the UDP stream — anyone on the LAN could send audio
  to the port. Fine for a demo, not for anything exposed beyond your LAN.
- Volume pot only scales digital samples (max ~90% of full scale to leave
  headroom); it doesn't touch the amp's analog gain.
