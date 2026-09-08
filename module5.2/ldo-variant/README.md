# Module 5.2 — PCB Layout: ESP32-S3 Power Supply (LDO Variant)

This routes the schematic from Module 5.1 (`esp32s3_power_ldo.kicad_sch` /
`.kicad_pro`, copied unchanged into this folder so the project is
self-contained and openable as one KiCad project) into a fully-routed,
DRC-clean 2-layer PCB (`esp32s3_power_ldo.kicad_pcb`).

The board was built and routed programmatically with KiCad 10's `pcbnew`
Python API (real footprints loaded straight from the system footprint
libraries, real pad geometry, real tracks/vias) rather than hand-edited —
see the "How it was built" note at the end.

## Circuit recap

USB-C (power-only) receptacle → AMS1117-3.3 LDO → ESP32-S3-WROOM-1 module,
with a 10k pull-up + 100nF cap + push-button reset network on EN, and
5.1k pull-downs on the USB-C CC1/CC2 lines (required so a USB-C source
enables VBUS for a non-PD sink). 10 components, 6 nets (`+5V`, `+3V3`,
`GND`, `/EN`, `/CC1`, `/CC2`).

## Board outline

Rectangular, **42 mm × 54 mm** (Edge.Cuts at X 0–42 mm, Y 12–66 mm in the
board's internal coordinate frame). A plain rectangle was chosen because
the circuit is small and doesn't benefit from a shaped outline; the size
is simply "everything placed with ~2 mm clearance to the nearest edge,"
which keeps the board compact (this is a power-supply daughterboard, not
a product enclosure) while leaving room to route by hand/script without
0201-class trace-to-pad clearances anywhere.

## Component placement rationale

**ESP32-S3-WROOM-1 (U2)** is placed unrotated with its antenna edge facing
the board's north (top) edge. The module's own footprint (`RF_Module:
ESP32-S3-WROOM-1`) defines a **keepout rule area** on F.Cu — a 48 mm × 21 mm
zone that extends from the antenna edge outward, in which KiCad's DRC
disallows copper (see `courtyard_overlap`/keepout checks; this is a real,
verified library footprint, not something I hand-drew). The board's top
edge is placed only ~2 mm north of the module body, so almost all of that
21 mm keepout simply falls **off the board** (there's no copper there
because there's no board there); the small sliver that *is* on-board
(between the board edge and the module body) is left empty — nothing is
routed or placed there. Every other component is kept south of the
keepout's southern boundary line (module-center-Y − 6.75 mm), so none of
them can ever intrude into the rule area regardless of X position. DRC
(`--schematic-parity`, which also checks rule areas) confirms zero
violations, so this is verified, not just argued.

**J1 (USB-C receptacle)** sits at the board's west edge, rotated 90° so
its cable-facing side points off the left edge of the board — a
power connector belongs on/near a board edge for cable access.

**U1 (AMS1117-3.3 LDO, SOT-223)** sits between J1 and U2, oriented so its
VI pin (pin 3) faces the incoming +5V side and its VO/tab (pin 2) faces
the +3V3 side that feeds the module — i.e. power flows physically
left-to-right through the board, mirroring the schematic's signal flow.
There's clear copper-free space around its TO-263-style tab (only two
short 0.6 mm traces touch pins 1/3; nothing else is routed under the
package) for its modest heat dissipation at these currents.

**C1 (10 µF, input bulk cap)** sits directly next to U1's VI pin;
**C2 (10 µF, output bulk cap)** sits directly next to U1's VO/tab pin and
also feeds U2's 3V3 pin — both decoupling caps are within ~3–6 mm of the
pins they decouple, as required for an LDO to stay stable and suppress
input/output ripple.

**R1/R2 (5.1k CC pull-downs)** sit right next to J1, since they only
connect to J1's CC1/CC2 pins and GND — no reason to route them far away.

**R3 (10k EN pull-up) / C3 (100nF EN cap) / SW1 (reset button)** form the
EN network next to U2's EN pin. **SW1 is placed at the board's south
(bottom) edge** (x=8, y=60, with the board extending to y=66) specifically
so it's reachable from the board edge for a user to press, rather than
being buried in the middle of the board.

## Routing / layer usage

This is a genuinely dense little board (a WROOM module's power pins are
tightly clustered, and the USB-C receptacle's CC/VBUS/GND pads sit on a
~1 mm pitch after rotation), so both copper layers are used:

- **F.Cu** carries the bulk of the routing — the main GND tree, the +5V
  run from J1→C1→U1, the +3V3 run from U1→C2→R3→U2, and the EN chain
  R3→C3→U2.
- **B.Cu** is used for a handful of deliberate layer-hops (13 vias total)
  where an F.Cu path would otherwise have crossed a different net's trace
  in this tight layout:
  - The CC1 and CC2 escape routes from J1's tightly-packed pad row (pads
    are ~1 mm pitch there) dive to B.Cu right next to the pad and surface
    again next to R1/R2.
  - U2's two GND pins (pin 1 on the west edge, pin 40 on the east edge)
    and its central thermal pad (pin 41) are tied together on B.Cu,
    passing underneath the module rather than routing across its top
    (which would have meant either crossing the RF keepout or weaving
    between 30+ unused NC pins on F.Cu).
  - The +3V3 hop from C2 to R3, and the EN hop from C3 to SW1, use short
    B.Cu detours to clear other nets in the crowded area between U1, R3,
    C3, J1, R1 and R2.

  Every one of these B.Cu segments was chosen specifically to avoid a
  same-layer clearance/short DRC violation with a neighboring different-net
  trace — confirmed by iterating `kicad-cli pcb drc` to zero errors.

- A direct GND tie was added between J1's two shield-pad clusters
  (its four "SH" pads are PTH, so they carry copper on **both** layers —
  routes near them had to be planned on both layers, not just F.Cu).

No copper zone/pour was used (see "Nice-to-have" below) — GND is fully,
manually routed instead, which also side-steps having to hand-carve a
keepout cutout for the RF rule area in a zone.

## Trace widths

| Net class | Width | Why |
|---|---|---|
| `+5V`, `+3V3`, `GND` | **0.6 mm** | Power/return nets. At the currents involved here (LDO output ~≤1 A max, USB input ~≤1.5 A), IPC-2221 gives comfortable temperature rise headroom at 0.6 mm/1 oz copper (well under 1 A/mm rule-of-thumb loading), while still being narrow enough to route through the tight areas around J1 and U2. GND additionally carries the return current for everything, so it gets the same wide class rather than a thinner one. |
| `/EN`, `/CC1`, `/CC2` | **0.25 mm** | Pure logic/control signals (EN reset line, USB-C CC resistor legs) carry negligible current — standard 0.25 mm keeps them easy to route between tightly-pitched pads without needing the extra clearance a wider trace would demand. |

Via pads are 0.6–0.7 mm with 0.3–0.35 mm drills (standard capable-fab
sizes), used only where a net needed to change layers.

## Layer stackup

Standard 2-layer board: **F.Cu / B.Cu**, no internal layers — appropriate
for a 10-component, 6-net power/regulator board with no high-speed
signals (the only "fast" signals — ESP32-S3's native USB D+/D- — aren't
even wired in this LDO-variant schematic, since it's power-only).

## Design-rule accommodation

The `RF_Module:ESP32-S3-WROOM-1` footprint's built-in thermal-pad-array
(under pin 41) uses an array of small **0.2 mm** drilled vias as part of
the library footprint itself (real, unmodified library data). KiCad's
default board minimum-hole-size constraint (0.3 mm) would flag these as
`drill_out_of_range`, so the board's minimum-hole design rule was relaxed
to 0.15 mm — 0.2 mm drills are within the capability of most PCB fabs, so
this is a reasonable, documented accommodation rather than a design flaw.

## DRC — final clean report

Run with:
```
kicad-cli pcb drc --severity-all --exit-code-violations --schematic-parity esp32s3_power_ldo.kicad_pcb
```

```
** Drc report for esp32s3_power_ldo.kicad_pcb **
** Created on 2026-08-28T22:45:25 **
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

Exit code: `0`. Zero errors, zero warnings, zero unconnected nets, zero
schematic-parity mismatches (every net and footprint on the board matches
the Module 5.1 schematic 1:1). The "Ignored checks" list is KiCad's
standard set of checks that are off by default (e.g. courtyard-definition
nagging, footprint-filter mismatches) — nothing was silenced to get a
clean run; every check that ran, passed.

## Nice-to-have not done: copper pour

A GND copper pour was intentionally **not** added. The board is fully,
manually GND-routed (per the assignment's explicit "manual GND routing is
also acceptable" allowance), which was the pragmatic choice here because
a pour would need a hand-carved keepout cutout matching the RF module's
48×21 mm rule area to avoid a DRC conflict — extra complexity for a board
this small where manual routing already gives every GND pin a solid, short
path back to the source.

## How it was built

The board was constructed with a Python script driving KiCad 10's
`pcbnew` module: real footprints were loaded from the system footprint
libraries (`FootprintLoad`), placed at explicit coordinates/rotations,
wired to `NETINFO_ITEM` nets matching the Module 5.1 netlist pad-for-pad,
and connected with explicit `PCB_TRACK`/`PCB_VIA` objects — then validated
by running `kicad-cli pcb drc --schematic-parity` and iterating on the
reported violations (crossing tracks, shorts, courtyard overlaps, drill
rules) until the report above was clean.
