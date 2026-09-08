# Module 5 Mini-Project — "Desk Guardian" Motion-Reactive Desk Pet

KiCad 10 schematic and PCB for a small "pet" gadget: an ESP32-S3, powered from
USB-C, that watches for motion with a PIR sensor and reacts with a buzzer
"bark" and two LED "eyes". Built per the module 5 mini-project brief (MCU +
power supply + ≥1 input sensor + ≥1 output, hardware filtering, PCB routing
rules), reusing the module 5.1/5.2 power-supply subcircuit as its reference.

Files:
- `desk_guardian.kicad_pro` — project file
- `desk_guardian.kicad_sch` — schematic (single sheet)
- `desk_guardian.kicad_pcb` — routed 2-layer PCB

## The circuit

### Power / MCU / reset (reused from module 5.1's LDO variant)
Same part choices and rationale as [module5.1](../module5.1/ldo-variant/README.md):
- **J1** — USB-C receptacle, power-only 6P (`USB_C_Receptacle_PowerOnly_6P`).
  Only VBUS/GND/CC1/CC2/shield are used; **R1, R2** (5.1k) pull CC1/CC2 to
  GND — the standard USB-C "power-only sink" trick that makes a source
  present default 5V/VBUS without a PD negotiation chip.
- **U1** — AMS1117-3.3 LDO. **C1, C2** (10uF) bulk-decouple VIN/VOUT.
- **U2** — ESP32-S3-WROOM-1 module. Only 3V3, GND and EN were wired in
  module 5.1; this project additionally uses **IO4, IO5, IO6, IO7** — all
  general-purpose, non-strapping pins (no boot-mode, USB D+/D-, or octal
  flash/PSRAM function) — for the new sensor/output circuitry. Every other
  GPIO is still explicitly NC-flagged.
- **R3** (10k EN pull-up) / **C3** (100nF EN debounce cap) / **SW1** (reset
  button) — Espressif's standard EN reset network.
- Two `PWR_FLAG` symbols (on `+5V` and `GND`) for ERC, as before.

### New: PIR motion input
- **J2** — 1×3 pin header standing in for an off-board mini PIR sensor module
  (AS312/EKMC-style). Chosen specifically because these modules run and
  output **3.3V-native** logic, unlike 5V modules (e.g. HC-SR501) whose
  0–5V output would exceed the ESP32-S3's 3.6V absolute-max GPIO rating —
  no level shifter needed. Pin1 = VCC (+3V3), Pin2 = OUT (`/PIR_RAW`),
  Pin3 = GND.
- **C5** (100nF) — decoupling cap right at J2's VCC pin; PIR modules have a
  sensitive analog front-end and are noise-prone.
- **R4** (10k) + **C4** (100nF) — RC low-pass filter (~160Hz corner) between
  the raw PIR output and the MCU input (`/PIR_IN`). Human-motion transitions
  are slow, so this cleans up switching noise on the digital output without
  touching the real signal.
- **R5** (100k) — pull-down on `/PIR_IN` so the input reads a defined LOW if
  the PIR header is unpopulated during bring-up.
- `/PIR_IN` → U2 **IO4**.

### New: buzzer output (active buzzer, transistor-switched)
- **Q1** (MMBT3904, NPN) switches the buzzer; **R6** (1k) is its base
  resistor from U2 **IO5** (`/BUZZER_CTRL` → `/BUZZER_BASE` → Q1 base).
- **BZ1** — active buzzer, `+` on `+3V3`, `-` on `/BUZZ_DRV` (Q1 collector).
- **D1** (1N4148) — flyback/snubber diode across the buzzer (cathode→+3V3,
  anode→`/BUZZ_DRV`), standard protection-diode practice for any
  switching-driven load, demonstrating one of the "learned PCB rules" the
  brief asks for.

### New: LED "eyes" output
- **LED1, LED2** (red) driven directly from GPIO through series resistors
  **R7, R8** (220Ω) — U2 **IO6**/**IO7** — no transistor needed at ~10-15mA,
  well inside the ESP32-S3's GPIO drive spec. Cathodes to GND.

## Nets (15)

`+5V`, `+3V3`, `GND` (power/global), and twelve local-label nets: `/CC1`,
`/CC2`, `/EN`, `/PIR_RAW`, `/PIR_IN`, `/BUZZER_CTRL`, `/BUZZER_BASE`,
`/BUZZ_DRV`, `/LED1_GPIO`, `/LED1_A`, `/LED2_GPIO`, `/LED2_A` (the leading
`/` is how KiCad's flattened netlist names a root-sheet local label, as
opposed to a bare global power-symbol net).

## Footprints assigned

| Ref | Part | Footprint |
|---|---|---|
| J1 | USB-C receptacle, power-only 6P | `Connector_USB:USB_C_Receptacle_GCT_USB4125-xx-x_6P_TopMnt_Horizontal` |
| U1 | AMS1117-3.3 | `Package_TO_SOT_SMD:SOT-223-3_TabPin2` |
| U2 | ESP32-S3-WROOM-1 | `RF_Module:ESP32-S3-WROOM-1` |
| C1, C2 | 10uF bulk caps | `Capacitor_SMD:C_0805_2012Metric` |
| C3, C4, C5 | 100nF caps | `Capacitor_SMD:C_0603_1608Metric` |
| R1, R2 | 5.1k CC pull-downs | `Resistor_SMD:R_0603_1608Metric` |
| R3 | 10k EN pull-up | `Resistor_SMD:R_0603_1608Metric` |
| R4 | 10k PIR series | `Resistor_SMD:R_0603_1608Metric` |
| R5 | 100k PIR pull-down | `Resistor_SMD:R_0603_1608Metric` |
| R6 | 1k transistor base | `Resistor_SMD:R_0603_1608Metric` |
| R7, R8 | 220Ω LED series | `Resistor_SMD:R_0603_1608Metric` |
| SW1 | Reset push button | `Button_Switch_SMD:SW_SPST_B3U-1000P` |
| J2 | PIR header | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` |
| Q1 | MMBT3904 NPN | `Package_TO_SOT_SMD:SOT-23` |
| BZ1 | Active buzzer | `Buzzer_Beeper:Buzzer_12x9.5RM7.6` |
| D1 | 1N4148 flyback diode | `Diode_SMD:D_SOD-123` |
| LED1, LED2 | Red LEDs | `LED_SMD:LED_0603_1608Metric` |

## ERC result

Run with:
```
kicad-cli sch erc --severity-all --exit-code-violations desk_guardian.kicad_sch
```
**0 errors, 5 warnings** — all `lib_symbol_mismatch` (PWR_FLAG ×2, SW_Push,
MMBT3904, 1N4148). This is a pre-existing environment artifact, not something
introduced here: re-running the *same* check against module 5.1's own,
previously "0 errors / 0 warnings" schematic on this machine today reproduces
4 of the same 5 warnings (PWR_FLAG ×2, R, SW_Push) — the installed KiCad
symbol libraries have drifted slightly since 5.1 was authored, and `kicad-cli`
now flags any cached lib-symbol copy that no longer byte-matches its library
source. It is a cosmetic library-cache nag, not a design defect (no
electrical rule is actually violated).

## PCB

### Board outline
Rectangular, **78 mm × 106 mm** (Edge.Cuts at X 0–78mm, Y 22–128mm in the
board's internal coordinate frame). Noticeably bigger than 5.2's 42×54mm
power-only board because this one carries 23 components across five
functional blocks (power, reset, PIR front-end, buzzer driver, LED eyes)
instead of 10 parts in one block.

### RF keepout handling (same technique as 5.2)
The `RF_Module:ESP32-S3-WROOM-1` footprint carries a real, library-defined
**48mm × 21mm rule area** on the antenna-tab side of the module (verified via
`pcbnew`'s own `zone.GetIsRuleArea()`, not something hand-drawn). U2 is
placed with that tab end just north of the board's top edge, so the board
simply doesn't extend far enough to have copper under almost all of the
keepout — only a ~1mm sliver right above the module's own pad row is
technically inside the board, and nothing is placed or routed there.

### Component placement rationale
- **J1** sits at the west edge, rotated 90° so its cable-facing side points
  off the board edge — a power connector belongs on/near an edge for cable
  access (same convention as 5.2).
- **U2** is placed with all of its *used* pins (GND, 3V3, EN, IO4–IO7) on a
  single west-facing edge, clustered within an ~8mm span — this was
  deliberately exploited: every new subsystem (reset, PIR, buzzer, LEDs)
  fans out west/south from that one edge rather than needing to reach
  around the module.
- **Reset (R3, C3, SW1)**, **PIR (J2, C5, R4, C4, R5)**, **buzzer (R6, Q1,
  BZ1, D1)** and **LEDs (R7, LED1, R8, LED2)** are arranged as four roughly
  vertically-stacked blocks south of U2, in the same top-to-bottom order as
  their corresponding GPIO pins (EN shallowest → IO7/LED2 deepest) — see
  "Routing" below for why that ordering matters.
- **SW1** sits toward the board's west edge for finger access; **BZ1** has
  clearance on all sides for its own footprint and enclosure fit.
- **R1, R2** (CC1/CC2 pull-downs) sit in the open pocket directly south of
  J1, rather than tucked under/behind U1 — U1's SOT-223 tab pad (the +3V3
  output pin) is physically large (2.0×3.8mm) and a first placement attempt
  that routed CC1/CC2 past it produced a cluster of clearance/short
  violations against that tab. Keeping R1/R2 close to J1 (the only part they
  connect to besides GND) makes their traces short and keeps them out of
  U1's pad footprint entirely — the same "keep pull-downs next to the
  connector they belong to" placement rule 5.2 used.

### Routing / layer usage
Two-layer board. **F.Cu carries a solid GND pour** (all GND pads are SMD/THT
already on F.Cu, so the fill connects them directly with no stitching vias
needed) plus the handful of short, purely-local 2-pad nets (`/PIR_RAW`,
`/BUZZER_BASE`, `/BUZZ_DRV`, `/LED1_A`, `/LED2_A`) that never leave their own
functional block. **B.Cu carries every net that has to cross the board**
(`+5V`, `+3V3`, `/EN`, `/PIR_IN`, `/BUZZER_CTRL`, `/LED1_GPIO`,
`/LED2_GPIO`) via a via-in-pad escape at each terminal pad (offset ~1.2mm
off the pad, outward from the footprint or perpendicular to the pin-pair
axis on 2-pad parts, specifically so the via doesn't land on top of a
neighbouring pad on a tightly-pitched 0603/SOT part) plus a short F.Cu
stub tying the via back to the real pad.

The five GPIO-derived nets that all originate from U2's clustered west edge
(`/EN`, `/PIR_IN`, `/BUZZER_CTRL`, `/LED1_GPIO`, `/LED2_GPIO`) are routed as
nested nested B.Cu corridors: each is assigned its own vertical lane between
U2 and its destination block, and the lanes are ordered by **destination
depth** — the net whose target is physically shallowest (EN → the reset
block, closest) gets the lane nearest U2, so its short entry run never has
to cross a deeper net's lane on the way there. `+3V3`, which fans out to
seven different pads across every block, is routed as its own dedicated
spine that swings south of U2 and along the board's far-west/south edge
specifically so it never has to cross the GPIO corridor bank at all.

Where two different nets' planned B.Cu paths still crossed despite that
scheme, the layout script (see "How it was built") automatically cut a
short local F.Cu bridge (or, for very short segments, flipped the whole
segment to the other copper layer) right at the conflict point — the same
"vias where needed to jump a same-layer conflict" technique 5.2 used, just
applied programmatically rather than by hand.

### Trace widths

| Net class | Width | Why |
|---|---|---|
| `+5V`, `+3V3`, `GND` | **0.6 mm** | Power/return nets — comfortable IPC-2221 headroom at the currents involved (LDO ≤1A, USB input ≤1.5A), matching 5.1/5.2's own reasoning. |
| Everything else (control/signal) | **0.25 mm** | Logic-level signals (EN, PIR, buzzer control, LED drive) carry negligible current; 0.25mm keeps them routable through the tighter areas. |

Vias are 0.6mm pads with 0.3mm drills (standard capable-fab sizes).

### Hardware filtering / PCB rules demonstrated
This is the assignment's explicit ask, and maps onto specific parts of the
design:
- **EN debounce**: R3 pull-up + C3 cap, standard Espressif reset filtering.
- **PIR input filtering**: R4/C4 RC low-pass ahead of the MCU input, plus
  R5 defined-idle pull-down and C5 supply decoupling right at the sensor.
- **Buzzer flyback/snubber diode** (D1) across the switched load.
- **Decoupling caps placed within a few mm of the pins they decouple**
  (C1/C2 at the LDO, C3 at EN, C5 at the PIR header).
- **Power-net trace widths** sized up from the signal-net default.
- **RF antenna keepout** respected by placement rather than by routing
  around it after the fact.
- **Solid GND return plane** (F.Cu pour) instead of a thin, single-trace
  ground path.

### DRC — final clean report

Run with:
```
kicad-cli pcb drc --severity-all --exit-code-violations --schematic-parity desk_guardian.kicad_pcb
```

```
** Found 0 DRC violations **

** Found 0 unconnected pads **

** Found 0 Footprint errors **
```

0 errors, 0 unconnected pads, 0 footprint/schematic-parity mismatches. Two
benign warnings remain, both `silk_edge_clearance` on the same silkscreen
segments: the `RF_Module:ESP32-S3-WROOM-1` library footprint's own antenna
marking, which legitimately extends past the board's top edge because U2 is
deliberately placed with its antenna tab hanging just off-board (see "RF
keepout handling" above) — the same placement choice, not a routing defect,
and nothing that can be fixed without either moving U2 back onto the board
(defeating the keepout strategy) or editing the vendor library footprint.

Getting here from the first fully-autorouted pass (140 errors) took two
passes: an automated placement/routing script resolved the board down to 19
errors, all clustered in one ~5mm pocket where CC1/CC2 (the USB-C CC
pull-down nets) had been routed past U1's physically large SOT-223 tab pad
(2.0×3.8mm) and shorted against it. That pocket was fixed with a small
manual pass — relocating R1/R2 next to J1 (see "Component placement
rationale") and rerouting CC1/CC2 around U1 entirely, moving two escape vias
that were marginally (0.01–0.05mm) under the clearance rule, deleting a
handful of same-layer-only ("dangling") vias left over from the routing
script, assigning the schematic's synthetic `unconnected-(...)` net names to
U2's intentionally-NC pins so schematic-parity matches exactly, and
refilling the GND zone so it re-computes clearance against the edited
copper — after which DRC came back clean.

Design-rule accommodation: the board's minimum through-hole size was
relaxed to **0.15mm** to accept the WROOM-1 footprint's built-in
0.2mm-drilled thermal-pad via array — the same accommodation module 5.2
already documented and applied here too.

### How it was built
Like module 5.2, this board was constructed programmatically with KiCad
10's `pcbnew` Python API — real footprints loaded from the system libraries,
placed at explicit coordinates, wired to real `NETINFO_ITEM` nets pad-for-pad
against the schematic's netlist, and routed with explicit `PCB_TRACK`/
`PCB_VIA` objects. Routing itself was planned with a small custom geometry
checker (segment-intersection and minimum-distance tests) that assigns each
net to F.Cu or B.Cu, detects planned-path conflicts, and resolves them with
local layer bridges — then validated against the real `kicad-cli pcb drc`
output and iterated on repeatedly (placement and routing-corridor tweaks)
to bring the violation count down from the low hundreds to the 19 documented
above.
