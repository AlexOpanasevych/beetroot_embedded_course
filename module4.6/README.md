# Module 4.6 — STM32 mini oscilloscope (ADC + circular DMA, console readout)

Extends the ADC1 + DMA light-sensor exercise from earlier in module 4 into a small
"oscilloscope": ADC1 free-runs on **ADC1_IN1 (PA1)**, continuously sampling into a
circular DMA buffer, and every 200 ms the firmware prints **Vmin / Vmax / Vpp / Vavg**
for the last measurement window over **USART1** to a serial console.

## What it does

- `ADC1` samples PA1 continuously (`ContinuousConvMode` + `DMAContinuousRequests`,
  `ADC_SAMPLETIME_3CYCLES` for the fastest practical capture rate), DMA writes each
  12-bit sample into a 256-entry circular buffer (`scope_buffer`) that never has to
  be restarted.
- The DMA half-transfer and transfer-complete callbacks (`HAL_ADC_ConvHalfCpltCallback`
  / `HAL_ADC_ConvCpltCallback`) each scan the half of the buffer that just finished
  filling — the same "process one half while DMA fills the other" pattern the
  light-sensor exercise used — folding it into running min/max/sum accumulators.
- Every `REPORT_PERIOD_MS` (200 ms) the main loop snapshots and resets those
  accumulators (briefly disabling IRQs so a DMA callback can't tear the read),
  converts raw ADC codes to millivolts against a 3.3 V full-scale reference, and
  transmits a line like:

  ```
  Vmin=0.012V  Vmax=3.287V  Vpp=3.275V  Vavg=1.643V  (n=45568)
  ```

  `n` is the number of samples folded into that window — useful to sanity-check the
  effective sample rate.
- All formatting uses integer math (`snprintf` with `%lu`, splitting millivolts into
  whole/fractional parts by hand) rather than `%f`. STM32CubeIDE links against
  newlib-nano by default, which drops float support from `printf`/`snprintf` unless
  you add `-u _printf_float` to the linker flags — integer formatting sidesteps that
  gotcha entirely.

## Wiring

| Signal            | STM32F401 pin | Notes                                      |
|--------------------|---------------|---------------------------------------------|
| Scope probe input  | PA1           | ADC1_IN1, 0–3.3V only (do **not** exceed VDD) |
| USART1 TX          | PA9           | to USB–serial adapter RX                    |
| USART1 RX          | PA10          | to USB–serial adapter TX (unused by firmware, wired for symmetry) |

Open a serial terminal at **115200 8N1** on the adapter's COM port to see the readout.
Feed PA1 anything from 0–3.3V to see it move — a potentiometer wiper, the LDR divider
from the earlier light-sensor exercise, or a low-amplitude signal generator output
(clamped/attenuated to stay within 0–3.3V — this is *not* a real scope input, there's
no attenuation or protection on PA1).

## CubeMX configuration

`oscilloscope.ioc` in this folder configures ADC1/DMA/USART1/pins as described below.
Open it in STM32CubeMX (or STM32CubeIDE's `.ioc` editor) and hit **Generate Code** to
produce the full CubeIDE project shell (`Drivers/`, linker script, `.project`, etc.) —
`Core/Inc`/`Core/Src` will merge with the hand-written files already in this folder
since `KeepUserCode=true` preserves the `USER CODE BEGIN/END` sections. It was authored
by hand to match MicroXplorer's file format rather than exported from a live CubeMX
session, so double-check the ADC1 panel (Continuous Conversion Mode = Enable, DMA
Continuous Requests = Enable, Sampling Time = 3 Cycles) after opening it, before
generating.

If you'd rather build the project from scratch instead of using the `.ioc`, the
equivalent manual steps are:

1. **ADC1**: enable channel **IN1** on PA1, 12-bit resolution, independent mode.
2. Under ADC1 parameter settings: **Continuous Conversion Mode = Enable**,
   **DMA Continuous Requests = Enable**, Sampling Time = 3 Cycles.
3. Add a **DMA request** for ADC1 (DMA2 Stream0 in this project): Mode =
   **Circular**, Data Width = Half Word for both peripheral and memory.
4. **USART1**: Mode = Asynchronous, default 115200 8N1 (pins PA9/PA10 auto-assigned).
5. Clock config: default HSI 16 MHz, no PLL (unchanged from the light-sensor project)
   — plenty for a 115200 baud UART and a few-hundred-kHz ADC capture rate.

Regenerate, then drop in the files from this folder's `Core/Inc` and `Core/Src`
(they follow the same `USER CODE BEGIN/END` structure CubeMX expects).

## Known limitations

- PA1 is a bare ADC input with no input protection, attenuation, or AC coupling —
  it reads 0–3.3V DC-referenced signals only. It's a statistics readout, not a
  true dual-trace, triggered, timebase-accurate oscilloscope.
- The 200 ms report window is a fixed averaging period, not a triggered acquisition
  — fast transients shorter than one window will be captured in the min/max but not
  shown as a waveform (there's no waveform/plot output, only the four numbers).
