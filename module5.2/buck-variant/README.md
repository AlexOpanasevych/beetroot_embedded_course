# Module 5.2 - ESP32-S3 Power Supply PCB Layout (Buck Converter Variant)

KiCad 10 project: the schematic from Module 5.1 (`buck-variant`), routed into a
2-layer PCB. This folder is a self-contained copy of the Module 5.1 project
(schematic + project files) plus the new, fully routed board
(`esp32s3_power_buck.kicad_pcb`), so it opens as one normal KiCad project.

The board was built programmatically with KiCad's bundled Python/`pcbnew`
(placing real library footprints, creating nets/tracks/vias from the
schematic's netlist, and adding the board outline), rather than by hand in
the PCB editor - this made it easy to derive exact pad coordinates from the
real footprint geometry and to verify placement/clearance numerically before
each DRC pass.

## Netlist source of truth

The board reproduces the schematic's netlist exactly (verified with
`--schematic-parity`, see DRC report below):

| Net | Pads |
|---|---|
| `/VIN` | J1.1, C1.1, U1.1 (VIN) |
| `/SW_NODE` | U1.2 (OUT), D1.1 (K), L1.1 |
| `+3V3` | L1.2, C2.1, U1.4 (FB), R1.2, U2.2 (3V3) |
| `/EN` | R1.1, C3.2, SW1.1, U2.3 (EN) |
| `GND` | C1.2, C2.2, C3.1, D1.2 (A), J1.2, SW1.2, U1.3 (GND+tab), U1.5 (ON/OFF), U2.1, U2.40, U2.41 |

Footprints used are exactly the ones assigned in the Module 5.1 schematic
(real KiCad library parts, no substitutions):

| Ref | Part | Footprint |
|---|---|---|
| J1 | Barrel jack | `Connector_BarrelJack:BarrelJack_CUI_PJ-063AH_Horizontal` |
| U1 | LM2596S-3.3 | `Package_TO_SOT_SMD:TO-263-5_TabPin3` |
| D1 | SS34 | `Diode_SMD:D_SMA` |
| L1 | 33uH inductor | `Inductor_SMD:L_10.4x10.4_H4.8` |
| C1 | 100uF/25V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` |
| C2 | 220uF/16V | `Capacitor_THT:CP_Radial_D6.3mm_P2.50mm` |
| C3 | 100nF | `Capacitor_SMD:C_0805_2012Metric` |
| R1 | 10k | `Resistor_SMD:R_0805_2012Metric` |
| SW1 | Push button | `Button_Switch_THT:SW_PUSH_6mm` |
| U2 | ESP32-S3-WROOM-1 | `RF_Module:ESP32-S3-WROOM-1` |

## Board outline

A rectangular `Edge.Cuts` outline, 90 mm x 80 mm, origin at (0,0). Size was
picked after placement: it gives the ESP32-S3 module's antenna keep-out zone
a clear margin to the board edge, keeps the buck-converter cluster's TO-263
tab and barrel jack clear of the board edge with a few mm to spare, and
leaves comfortable room for the reset network without needing to crowd
anything. It's larger than the absolute minimum a professional layout would
use, but that headroom is what made it possible to route entirely by hand
(via a script) without fighting for millimeters.

## Placement rationale

**ESP32-S3-WROOM-1 (U2)** is rotated so its antenna keep-out area (the wide
skirt at one end of the module's `F.CrtYd` courtyard - a rectangle roughly
21 mm x 48 mm, clearly larger than the module body itself) faces the board's
west edge, with the rest of the board's circuitry to the east and south of
the module body. This keeps the antenna area open (no other footprints'
courtyards or copper anywhere near it) and moves the noisy switching
regulator to the opposite side of the board from the RF module, which is
good practice for both antenna performance and switching noise coupling into
the 2.4 GHz front end.

**Buck converter switching loop (U1, D1, L1, C1, C2)** is clustered in the
board's north-east corner, physically as close together as the parts' real
footprint envelopes allow:
- `C1` (input bulk cap) sits right at U1's VIN pin.
- `D1` (catch diode) and `L1` (inductor) sit right at U1's OUT/switch-node
  pin - this is the loop that matters most for a buck converter (VIN through
  the FET, out through L1, with D1 providing the return path when the switch
  is off), so U1-D1-L1 form a tight triangle a few mm on a side rather than
  being spread across the board.
- `C2` (output bulk cap) sits just past L1's output pin.

The real footprint sizes (the TO-263 tab, the 10.4x10.4 mm shielded
inductor, the SMA diode) set a practical lower bound on how tight this loop
can be; given those envelopes, the loop here is about as small as it can be
made without overlapping courtyards.

**Reset network (R1, C3, SW1)** sits just south of the ESP32 module's EN/3V3/
GND pin row, i.e. as close as possible to the pins it serves. `SW1` (the
physical reset button) is placed toward the south-east of that group, away
from other components, so it stays reachable if the board is mounted for
production access to the button.

**Barrel jack (J1)** is mounted on the board's south edge (rotated 180 deg so
its plug-facing side lines up with the board edge), the standard placement
for a panel/edge-mount power connector so the DC plug is accessible from
outside an enclosure.

## Trace widths

- **Power/switching nets** (`/VIN`, `/SW_NODE`, `+3V3`, `GND`): **0.8 mm**.
  These carry the LM2596's several-amp switching current (and its return
  path on GND), so a wider trace than signal-level routing is used to keep
  resistive drop and heating low, per the requirement to use ~0.6-1.0 mm for
  power nets on a buck converter board.
- **`/EN`** (reset/enable logic signal): **0.3 mm**. This is a low-current
  logic-level net (10k pull-up, 100nF cap, push button), so a standard thin
  signal trace is appropriate; no need for the extra copper power traces use.
- **Vias**: 0.8 mm pad / 0.4 mm drill (standard-size vias), used to move GND
  (and two short `/VIN` hops, see below) between layers.

## Layer stackup / GND routing strategy

Standard 2-layer board (F.Cu / B.Cu). `/VIN`, `/SW_NODE`, `+3V3` and `/EN`
are routed entirely on the top layer (F.Cu). **GND is routed as a dedicated
tree entirely on the bottom layer (B.Cu)**, reached from each SMD GND pad by
a short top-layer stub into a via, and connected directly (no via needed) at
every through-hole GND pad (C1, C2, SW1, J1 are all THT, so their GND pads
already have copper on both layers).

This was a deliberate choice over a full copper pour/zone: with only 10
parts and 5 nets, a manually-routed GND tree is simple to verify net-by-net,
and putting GND on its own layer means the four F.Cu nets can be routed
without having to dodge a GND pour, and automatically can't short into each
other's layer. It also sidesteps any question of copper near the ESP32
module's antenna keep-out area - the GND tree on B.Cu was routed clear of
that whole rectangle, and no zone/pour was added there either. A full GND
copper pour (the "nice-to-have" in the brief) was consciously skipped in
favor of finishing a clean, fully-verified manual route; the existing B.Cu
GND tree could be turned into a pour later by just adding a `B.Cu` zone over
the board outline (minus the antenna keep-out) with the existing tracks left
in place as reinforcement.

Two short `/VIN` segments (about 2 mm each, near x=52.5, y=48-50) are
deliberately routed on B.Cu via a via-hop, purely to cross under the `+3V3`
trunk trace on F.Cu at one unavoidable intersection between the ESP32
module's south side and the buck cluster - everywhere else `/VIN` stays on
F.Cu.

## Deviations / notes on the clean DRC result

- **Minimum hole size relaxed to 0.2 mm.** The official KiCad
  `RF_Module:ESP32-S3-WROOM-1` footprint defines a cluster of small
  0.2 mm-drill plated pads under pin 41 (the antenna/thermal ground contact
  area) as part of its vendor-defined land pattern - this is baked into the
  real, verified footprint, not a routing choice made here. The board's
  minimum-hole design rule was lowered from KiCad's 0.3 mm default to 0.2 mm
  so DRC checks the real footprint instead of flagging it as an error. If
  this exact module/footprint is sent to fab, confirm the chosen
  manufacturer supports a 0.2 mm minimum drill (many do, sometimes at a
  higher price tier); this is a property of the ESP32-S3-WROOM-1 module's
  official footprint, not something introduced by this layout.
- U1's footprint (`TO-263-5_TabPin3`) exposes GND (pin 3) as two separate
  copper pads - the small gull-wing lead in the pin row, and the large
  thermal tab - both numbered "3" but not touching in copper. A short 0.8 mm
  F.Cu trace ties them together explicitly so the connection is real copper,
  not just a shared pad number.
- U2's pin 41 (GND) is exposed as a big 3.9x3.9 mm pad plus many small
  0.6x0.6 mm stitching pads, all physically overlapping the big pad - these
  are already one electrically continuous copper feature from the
  footprint itself, so only one via was placed on it.

## Final DRC report

Command used:
```
kicad-cli pcb drc esp32s3_power_buck.kicad_pcb --severity-all --exit-code-violations --schematic-parity
```

```
** Drc report for esp32s3_power_buck.kicad_pcb **
** Created on 2026-08-28T22:44:40 **
** Report includes: Errors, Warnings, Exclusions **

** Found 0 DRC violations **

** Found 0 unconnected pads **

** Found 0 Footprint errors **

** Ignored checks **
    - Footprint has no courtyard defined
    - Track endpoint not centered on via
    - Tuning profile track geometries
    - Footprint doesn't match symbol's footprint filters
    - Footprint component type doesn't match footprint pads

** End of Report **
```

0 errors, 0 warnings, 0 unconnected pads, 0 schematic-parity issues (every
net and every footprint on the board matches the Module 5.1 schematic
exactly).
