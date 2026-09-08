#!/usr/bin/env python3
"""
Real-time RAW vs SMA vs EMA plotter for ESP32-S3 (module5.5).

Reads the fixed-width table rows produced by main.cpp (N / RAW / SMA / EMA / ...)
and plots all three signals against sample index (time), so you can see:
  - how much each filter smooths out ADC noise when the pot is held still
  - how much lag each filter adds when the pot is swept quickly

Usage
-----
  python serial_plotter.py COM3
  python serial_plotter.py /dev/ttyUSB0 --baud 115200 --window 400

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
from matplotlib.animation import FuncAnimation

DEFAULT_BAUD   = 115200
DEFAULT_WINDOW = 400   # last N samples kept in RAM


def parse_args():
    p = argparse.ArgumentParser(description="ESP32-S3 RAW/SMA/EMA real-time plotter")
    p.add_argument("port", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("--window", type=int, default=DEFAULT_WINDOW,
                   help="Rolling buffer size (samples)")
    return p.parse_args()


class DataBuffer:
    """Thread-safe rolling buffer for RAW/SMA/EMA samples."""

    def __init__(self, maxlen: int):
        self._lock = threading.Lock()
        self.n   = deque(maxlen=maxlen)
        self.raw = deque(maxlen=maxlen)
        self.sma = deque(maxlen=maxlen)
        self.ema = deque(maxlen=maxlen)

    def push(self, n: int, raw: int, sma: int, ema: int):
        with self._lock:
            self.n.append(n)
            self.raw.append(raw)
            self.sma.append(sma)
            self.ema.append(ema)

    def snapshot(self):
        with self._lock:
            return (np.array(self.n,   dtype=float),
                    np.array(self.raw, dtype=float),
                    np.array(self.sma, dtype=float),
                    np.array(self.ema, dtype=float))


def _parse_line(line: str):
    """
    Parse one table row produced by main.cpp:
      %-6lu  %-6d  %-6d  %-6d  %-8d  %-8d  %-8d
      N       RAW    SMA    EMA    RAW_mV SMA_mV EMA_mV
    Returns (n, raw, sma, ema) or None on failure.
    """
    line = line.strip()
    if not line or not line[0].isdigit():
        return None
    parts = line.split()
    if len(parts) < 4:
        return None
    try:
        n, raw, sma, ema = (int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3]))
        return n, raw, sma, ema
    except ValueError:
        return None


def serial_reader(ser: serial.Serial, buf: DataBuffer, stop: threading.Event):
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


def main():
    args = parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as exc:
        print(f"[ERROR] Cannot open {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {args.port} @ {args.baud} baud — "
          f"hold the pot still, then sweep it, to compare noise vs lag.")

    buf = DataBuffer(args.window)
    stop_event = threading.Event()
    t = threading.Thread(target=serial_reader, args=(ser, buf, stop_event), daemon=True)
    t.start()

    fig, ax = plt.subplots(figsize=(13, 6))
    fig.suptitle("ESP32-S3 ADC — RAW vs SMA vs EMA (live)", fontsize=13, fontweight="bold")

    line_raw, = ax.plot([], [], color="C7", lw=1.0, alpha=0.6, label="RAW (unfiltered)")
    line_sma, = ax.plot([], [], color="C0", lw=1.8, label=f"SMA")
    line_ema, = ax.plot([], [], color="C1", lw=1.8, label=f"EMA")
    ax.set_xlabel("Sample #")
    ax.set_ylabel("ADC raw counts (0–4095)")
    ax.set_ylim(-50, 4150)
    ax.legend(loc="upper left", fontsize=9)
    ax.grid(True, alpha=0.3)

    def update(_frame):
        n, raw, sma, ema = buf.snapshot()
        if n.size < 2:
            return
        line_raw.set_data(n, raw)
        line_sma.set_data(n, sma)
        line_ema.set_data(n, ema)
        ax.set_xlim(n.min(), n.max())

    ani = FuncAnimation(fig, update, interval=100, blit=False, cache_frame_data=False)

    try:
        plt.tight_layout()
        plt.show()
    finally:
        stop_event.set()
        ser.close()
        print("Serial port closed.")


if __name__ == "__main__":
    main()
