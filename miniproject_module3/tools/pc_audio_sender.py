"""Streams this PC's system audio (WASAPI loopback) to the ESP32 subwoofer
receiver as raw 16-bit PCM over UDP.

Setup:
    pip install soundcard numpy

Usage:
    python pc_audio_sender.py <esp32-ip> [--port 3333]

The ESP32 must already be on the same LAN and running the firmware in
src/main.c, which listens on --port (default from Kconfig: 3333).
"""

import argparse
import socket
import sys

import numpy as np
import soundcard as sc

SAMPLE_RATE_HZ = 44100
CHANNELS = 2
BLOCK_SAMPLES = 256  # frames per read; keeps UDP packets small and low-latency


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("esp32_ip", help="IP address of the ESP32 receiver")
    parser.add_argument("--port", type=int, default=3333, help="UDP port (must match CONFIG_AUDIO_UDP_PORT)")
    args = parser.parse_args()

    speaker = sc.default_speaker()
    loopback_mic = sc.get_microphone(speaker.name, include_loopback=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.esp32_ip, args.port)

    print(f"Capturing loopback from '{speaker.name}' -> {args.esp32_ip}:{args.port}")
    print("Ctrl+C to stop.")

    try:
        with loopback_mic.recorder(samplerate=SAMPLE_RATE_HZ, channels=CHANNELS) as recorder:
            while True:
                frames = recorder.record(numframes=BLOCK_SAMPLES)  # float32, [-1, 1], shape (N, 2)
                pcm16 = np.clip(frames * 32767.0, -32768, 32767).astype(np.int16)
                sock.sendto(pcm16.tobytes(), dest)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())
