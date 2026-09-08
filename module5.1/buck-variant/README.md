# Module 5.1 - ESP32-S3 Power Supply (Buck Converter Variant)

KiCad 10 schematic-only project for the ESP32-S3 power supply homework. This
variant uses a switching (buck) regulator instead of an LDO to step the
input supply down to 3.3 V.

Files:
- `esp32s3_power_buck.kicad_pro` - project file
- `esp32s3_power_buck.kicad_sch` - schematic (hand-authored KiCad 10 S-expression format)

## Circuit overview

```
DC jack (5-12V) --> Cin --> LM2596S-3.3 buck --> L --> Cout --> +3V3 --> ESP32-S3-WROOM-1
                                  |                                 |
                                 GND                          EN pull-up/cap/button
```

## Part choices

**Power input connector - J1, `Connector:Barrel_Jack`**
A standard 5.5x2.1 mm DC barrel jack (footprint
`Connector_BarrelJack:BarrelJack_CUI_PJ-063AH_Horizontal`) was chosen because a
buck converter is only worth using over an LDO when the input rail is
meaningfully above 3.3 V (typical wall-adapter values are 5-12 V). A barrel
jack is the standard connector for this kind of "plug in a wall adapter"
input, as opposed to USB or a 2-pin JST which usually implies a fixed 5 V or
battery source.

**Buck regulator - U1, `Regulator_Switching:LM2596S-3.3`**
The LM2596S-3.3 is a real, fixed-3.3V-output part present in KiCad's
`Regulator_Switching` symbol library (TO-263-5, footprint
`Package_TO_SOT_SMD:TO-263-5_TabPin3`). It's a classic, well-documented 3 A
step-down switcher that is simple to design around with a fixed output (no
external feedback divider needed - FB is simply tied to the filtered output).
Application circuit follows TI's standard reference design:
- `C1` (100 uF/25 V) - input bulk capacitor, VIN to GND
- `D1` (SS34, 40 V/3 A Schottky, footprint `Diode_SMD:D_SMA`) - catch/freewheel
  diode, cathode to the switching (OUT) node, anode to GND
- `L1` (33 uH, footprint `Inductor_SMD:L_10.4x10.4_H4.8`) - output inductor,
  from the switching node to the filtered +3.3 V rail
- `C2` (220 uF/16 V) - output bulk capacitor, +3.3 V to GND
- `FB` pin wired directly to the filtered +3.3 V output (fixed-voltage part,
  no external divider required)
- `ON/OFF` pin tied to GND (always enabled)

**Reset circuit - EN (CHIP_PU) network**
Per Espressif's standard reference design for the EN/reset pin:
- `R1` = 10 kOhm pull-up from +3.3 V to EN
- `C3` = 100 nF decoupling capacitor from EN to GND (slows the EN rise time
  for a clean power-on reset)
- `SW1` = momentary push button (`Switch:SW_Push`, footprint
  `Button_Switch_THT:SW_PUSH_6mm`) from EN to GND - pressing it pulls EN low
  and resets the module

**MCU module - U2, `RF_Module:ESP32-S3-WROOM-1`**
The real ESP32-S3-WROOM-1 module symbol (footprint
`RF_Module:ESP32-S3-WROOM-1`) is used so the schematic represents an actual
power path into the chip. Only `GND`, `3V3` and `EN` are wired; all other
41 pins are terminated with explicit no-connect flags since routing every
GPIO is out of scope for a power-supply schematic.

## Nets

- `VIN` - J1(+) - C1(+) - U1.VIN (connected via matching local labels, not one
  drawn wire, since the three points are far apart on the sheet)
- `SW_NODE` - U1.OUT - D1(cathode) - L1(pin 1) (switching/PWM node, local label)
- `+3V3` - L1(pin 2) - C2(+) - U1.FB - R1 - U2.3V3 (global power symbol)
- `GND` - J1(-), C1(-), U1.GND, U1.ON/OFF, D1(anode), C2(-), C3, SW1, U2.GND
  (global power symbol)
- `EN` - R1 - C3 - SW1 - U2.EN (local label)

Three `PWR_FLAG` symbols are placed on the `VIN`, `GND` and `+3V3` nets so
ERC recognizes them as externally-driven power nets (none of the pins on
those nets are natively an "Output"/"Power output" type once the
inductor separates the switching node from the filtered output rail, which
is the normal situation for any buck-regulator schematic).

## ERC result (final, clean run)

Command used:
```
kicad-cli sch erc esp32s3_power_buck.kicad_sch --severity-all --exit-code-violations
```

```
ERC report (2026-08-28T22:13:50, Encoding UTF8)
Report includes: Errors, Warnings, Exclusions

***** Sheet /
[lib_symbol_mismatch]: Symbol 'SS34' doesn't match copy in library 'Diode'
    ; warning
    @(101.60 mm, 73.66 mm): Symbol D1 [SS34]

 ** ERC messages: 1  Errors 0  Warnings 1

 ** Ignored checks:
    - Global label only appears once in the schematic
    - Four connection points are joined together
    - SPICE model issue
    - Assigned footprint doesn't match footprint filters
```

**0 errors.** The single remaining warning is cosmetic: the SS34 symbol in
KiCad's `Diode` library is defined via an `extends "SB120"` inheritance
relationship, and this schematic's cached copy of it was expanded into a
standalone symbol so it could be hand-authored without a scripting API. The
electrical pin data is identical either way (verified: reproducing the
`extends` relationship exactly instead produced the same footprint/pins but
triggered spurious "unconnected" errors from KiCad's extends-resolution in
`kicad-cli`, so the flattened, ERC-clean form was kept). It does not affect
netlist, ERC connectivity, or PCB export.
