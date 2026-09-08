# Module 5.1 - ESP32-S3 Power Supply (LDO Variant)

KiCad 10 schematic for an ESP32-S3-WROOM-1 power supply built around a linear (LDO)
regulator, per the module 5.1 homework brief: power connector + converter (LDO/buck)
+ reset button, feeding an ESP32-S3.

Files:
- `esp32s3_power_ldo.kicad_pro` - project file
- `esp32s3_power_ldo.kicad_sch` - schematic (single sheet)

## Part choices

**Power connector - USB-C receptacle, power-only (J1, `USB_C_Receptacle_PowerOnly_6P`,
footprint `USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal`)**
A modern 5 V source instead of a barrel jack. Only the 6 power-relevant pins of the
Type-C connector are used: VBUS, GND, the two CC lines and the shield. CC1/CC2 are
each pulled to GND through 5.1 kOhm resistors (R1, R2) - this is the standard
Type-C "power-only sink" trick that tells any USB-C source (charger, power bank,
host) to present default 5 V/VBUS without needing a full USB-PD/BFSK negotiation
chip. The shield is tied to GND.

**Converter - AMS1117-3.3 LDO (U1, SOT-223, footprint `SOT-223-3_TabPin2`)**
A classic, cheap, widely-available 1 A fixed 3.3 V linear regulator - the standard
teaching example for an LDO-based supply. Input (VIN) and output (VOUT) each get a
10 uF bulk capacitor (C1, C2) to GND, matching the datasheet reference design and
giving the LDO the load-transient headroom it needs. Dropout from 5 V to 3.3 V is
comfortably within the AMS1117's ~1.1 V dropout spec.

**Reset circuit on EN (SW1, R3, C3)**
Follows Espressif's standard ESP32 reference reset circuit:
- R3 = 10 kOhm pull-up from 3V3 to EN, so EN idles high (module runs) by default.
- C3 = 100 nF decoupling capacitor from EN to GND, which slows the EN edge enough
  to debounce the button and avoid spurious resets from supply noise.
- SW1 = momentary push button (`SW_Push`, footprint `SW_SPST_B3U-1000P`) from EN to
  GND - pressing it pulls EN low and resets the chip; releasing lets the pull-up
  bring EN back high.

**MCU - ESP32-S3-WROOM-1 module (U2)**
The actual RF module symbol/footprint (`RF_Module:ESP32-S3-WROOM-1`) is placed so
the schematic represents real power delivery into the chip, not just an abstract
rail. Only 3V3, GND and EN are wired; the 36 unused GPIO/RXD0/TXD0/USB pins are
each terminated with a no-connect flag so ERC stays clean without fanning them out
to nowhere.

## Nets

- `+5V` - J1.VBUS, U1.VI (pin 3), C1 (input cap)
- `+3V3` - U1.VO (pin 2, the only power-output pin driving this net), C2 (output
  cap), R3 pull-up, U2.3V3
- `GND` - J1.GND/SHIELD, U1.GND, C1/C2/C3 return pins, R1/R2 return pins, SW1,
  U2.GND (incl. its two additional hidden GND pads)
- `CC1` / `CC2` - J1 CC lines to their respective 5.1 kOhm pull-downs
- `EN` - U2.EN, R3, C3, SW1

Two `PWR_FLAG` symbols (on the `GND` net and the `+5V` net) tell ERC these rails are
legitimately externally driven, since the USB-C connector's VBUS/GND pins are typed
"passive" rather than "power output".

## Footprints assigned

| Ref | Part | Footprint |
|---|---|---|
| J1 | USB-C receptacle, power-only 6P | `Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal` |
| U1 | AMS1117-3.3 | `Package_TO_SOT_SMD:SOT-223-3_TabPin2` |
| U2 | ESP32-S3-WROOM-1 | `RF_Module:ESP32-S3-WROOM-1` |
| C1, C2 | 10 uF bulk caps | `Capacitor_SMD:C_0805_2012Metric` |
| C3 | 100 nF EN decoupling | `Capacitor_SMD:C_0603_1608Metric` |
| R1, R2 | 5.1k CC pull-downs | `Resistor_SMD:R_0603_1608Metric` |
| R3 | 10k EN pull-up | `Resistor_SMD:R_0603_1608Metric` |
| SW1 | Reset push button | `Button_Switch_SMD:SW_SPST_B3U-1000P` |

All are ready for module 5.2 (PCB layout from this schematic).

## ERC result (final, clean)

Run with:
```
kicad-cli sch erc esp32s3_power_ldo.kicad_sch --severity-all --exit-code-violations
```

```
ERC report (2026-08-28T22:06:12, Encoding UTF8)
Report includes: Errors, Warnings, Exclusions

***** Sheet /

 ** ERC messages: 0  Errors 0  Warnings 0

 ** Ignored checks:
    - Global label only appears once in the schematic
    - Four connection points are joined together
    - Assigned footprint doesn't match footprint filters
```

Exit code: `0`. 0 errors, 0 warnings.
