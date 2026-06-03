#!/usr/bin/env python3
"""
Real-time ADC serial plotter for ESP32-S3 (module3.1).

Reads the fixed-width table rows produced by main.cpp (RAW / U_manual / U_cali / Error)
and displays:
  - Top plot   : U_manual and U_cali vs RAW (transfer-function view)
  - Bottom plot: Error % vs RAW
  - Red shading wherever Error > threshold  → "Ділянки нелінійності"

Usage
-----
  python serial_plotter.py COM3
  python serial_plotter.py /dev/ttyUSB0 --baud 115200 --threshold 3.0 --window 600

Requirements
------------
  pip install pyserial matplotlib numpy
"""

import sys
import argparse
import threading
import serial
import numpy as np
from collections import deque
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.animation import FuncAnimation

# ── defaults ──────────────────────────────────────────────────────────────────
DEFAULT_BAUD      = 115200
DEFAULT_WINDOW    = 600   # last N samples kept in RAM
DEFAULT_THRESHOLD = 3.0   # error-% above which a region is "non-linear"


# ─────────────────────────────────────────────────────────────────────────────
def parse_args():
    p = argparse.ArgumentParser(description="ESP32-S3 ADC real-time plotter")
    p.add_argument("port",
                   help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    p.add_argument("--baud",      type=int,   default=DEFAULT_BAUD)
    p.add_argument("--window",    type=int,   default=DEFAULT_WINDOW,
                   help="Rolling buffer size (samples)")
    p.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                   help="Error %% threshold for non-linearity highlight")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
class DataBuffer:
    """Thread-safe rolling buffer for ADC measurements."""

    def __init__(self, maxlen: int):
        self._lock   = threading.Lock()
        self.raw      = deque(maxlen=maxlen)
        self.u_manual = deque(maxlen=maxlen)
        self.u_cali   = deque(maxlen=maxlen)
        self.error    = deque(maxlen=maxlen)

    def push(self, raw: int, um: float, uc: int, err: float):
        with self._lock:
            self.raw.append(raw)
            self.u_manual.append(um)
            self.u_cali.append(uc)
            self.error.append(err)

    def snapshot(self):
        with self._lock:
            return (np.array(self.raw,      dtype=float),
                    np.array(self.u_manual, dtype=float),
                    np.array(self.u_cali,   dtype=float),
                    np.array(self.error,    dtype=float))


# ─────────────────────────────────────────────────────────────────────────────
def _parse_line(line: str):
    """
    Try to parse one table row produced by main.cpp:
      %-6d  %-14.1f  %-12d  %-10.2f
    Returns (raw, u_manual, u_cali, error) or None on failure.
    """
    line = line.strip()
    if not line or not line[0].isdigit():
        return None
    parts = line.split()
    if len(parts) < 4:
        return None
    try:
        raw = int(parts[0])
        um  = float(parts[1])
        uc  = int(parts[2])   # "N/A" raises ValueError → skip
        err = float(parts[3])
        return raw, um, uc, err
    except ValueError:
        return None


def serial_reader(ser: serial.Serial, buf: DataBuffer, stop: threading.Event):
    """Background thread: drain the serial port and push parsed rows into buf."""
    while not stop.is_set():
        try:
            raw_line = ser.readline()
        except serial.SerialException:
            break
        try:
            text = raw_line.decode("utf-8", errors="ignore")
        except Exception:
            continue
        parsed = _parse_line(text)
        if parsed is not None:
            buf.push(*parsed)


# ─────────────────────────────────────────────────────────────────────────────
def _add_nonlinear_spans(ax, x_sorted, err_sorted, threshold, color="red", alpha=0.18):
    """
    Shade x-axis intervals where err > threshold.
    Returns list of added PolyCollection artists (for later removal).
    """
    spans = []
    in_nl  = False
    start  = None
    for xi, ei in zip(x_sorted, err_sorted):
        if ei > threshold and not in_nl:
            in_nl = True
            start = xi
        elif ei <= threshold and in_nl:
            in_nl = False
            spans.append(ax.axvspan(start, xi, alpha=alpha, color=color, zorder=0))
    if in_nl and start is not None:
        spans.append(ax.axvspan(start, float(x_sorted[-1]),
                                alpha=alpha, color=color, zorder=0))
    return spans


# ─────────────────────────────────────────────────────────────────────────────
def main():
    args = parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        print(f"[ERROR] Cannot open {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {args.port} @ {args.baud} baud — sweep the potentiometer!")

    buf        = DataBuffer(args.window)
    stop_event = threading.Event()
    t = threading.Thread(target=serial_reader,
                         args=(ser, buf, stop_event), daemon=True)
    t.start()

    # ── Figure / axes ─────────────────────────────────────────────────────────
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(13, 8), sharex=True)
    fig.suptitle("ESP32-S3 ADC — Потенціометр (live)",
                 fontsize=13, fontweight="bold")

    line_manual, = ax1.plot([], [], color="C0", lw=1.8,
                            label="U_manual  (лінійна формула)")
    line_cali,   = ax1.plot([], [], color="C2", lw=1.8,
                            label="U_cali  (калібрований АЦП)")
    nl_patch = mpatches.Patch(color="red", alpha=0.4,
                               label="Ділянка нелінійності")
    ax1.set_ylabel("Напруга (mV)")
    ax1.set_xlim(0, 4095)
    ax1.set_ylim(-50, 3300)
    ax1.legend(handles=[line_manual, line_cali, nl_patch],
               loc="upper left", fontsize=9)
    ax1.grid(True, alpha=0.3)

    line_err, = ax2.plot([], [], color="C3", lw=1.5, label="Похибка (%)")
    thr_line  = ax2.axhline(args.threshold, color="orange", ls="--", lw=1.2,
                             label=f"Поріг {args.threshold}%")
    ax2.set_ylabel("Похибка (%)")
    ax2.set_xlabel("RAW  (12-біт ADC, 0 – 4095)")
    ax2.set_ylim(-0.5, 15)
    ax2.legend(loc="upper right", fontsize=9)
    ax2.grid(True, alpha=0.3)

    status = ax2.text(0.02, 0.93, "Очікую дані…",
                      transform=ax2.transAxes, fontsize=9, va="top",
                      bbox=dict(boxstyle="round,pad=0.3", fc="lightyellow", alpha=0.9))

    active_spans: list = []   # spans added in last frame

    # ── Animation update ──────────────────────────────────────────────────────
    def update(_frame):
        nonlocal active_spans

        raw, um, uc, err = buf.snapshot()
        if raw.size < 3:
            return

        # Sort by RAW to get the transfer-function curve
        idx  = np.argsort(raw)
        rx   = raw[idx]
        umx  = um[idx]
        ucx  = uc[idx]
        ex   = err[idx]

        line_manual.set_data(rx, umx)
        line_cali.set_data(rx, ucx)
        line_err.set_data(rx, ex)

        # ── Remove previous non-linearity spans ───────────────────────────────
        for sp in active_spans:
            try:
                sp.remove()
            except Exception:
                pass
        active_spans.clear()

        # ── Add new spans ─────────────────────────────────────────────────────
        active_spans.extend(_add_nonlinear_spans(ax1, rx, ex, args.threshold))
        active_spans.extend(_add_nonlinear_spans(ax2, rx, ex, args.threshold))

        # ── Status annotation ─────────────────────────────────────────────────
        n_nl    = int(np.sum(ex > args.threshold))
        n_total = len(ex)

        if n_nl:
            nl_raw  = rx[ex > args.threshold]
            raw_min = int(nl_raw.min())
            raw_max = int(nl_raw.max())
            # Find all contiguous non-linear intervals for the status text
            intervals = []
            in_nl     = False
            r_start   = None
            for xi, ei in zip(rx, ex):
                if ei > args.threshold and not in_nl:
                    in_nl   = True
                    r_start = xi
                elif ei <= args.threshold and in_nl:
                    in_nl = False
                    intervals.append(f"{int(r_start)}–{int(xi)}")
            if in_nl:
                intervals.append(f"{int(r_start)}–{int(rx[-1])}")
            zones = ", ".join(intervals[:4])  # show at most 4 zones
            status.set_text(
                f"Ділянки нелінійності: {n_nl}/{n_total} точок\n"
                f"RAW зони: [{zones}]  (поріг {args.threshold}%)"
            )
            status.set_bbox(dict(boxstyle="round,pad=0.3", fc="salmon", alpha=0.9))
        else:
            status.set_text(
                f"Нелінійності не виявлено  ({n_total} точок, "
                f"max err = {ex.max():.2f}%)"
            )
            status.set_bbox(dict(boxstyle="round,pad=0.3", fc="lightgreen", alpha=0.9))

    ani = FuncAnimation(fig, update, interval=250, blit=False, cache_frame_data=False)

    try:
        plt.tight_layout()
        plt.show()
    finally:
        stop_event.set()
        ser.close()
        print("Serial port closed.")


if __name__ == "__main__":
    main()
