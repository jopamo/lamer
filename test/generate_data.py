#!/usr/bin/env python3
"""Generate synthetic WAV test fixtures for the LAME test suite.

All fixtures are deterministic — given the same Python version, they produce
byte-identical output. Run this script to regenerate the test audio files
in test/data/. The script is idempotent: it skips files that already exist
unless --force is given.

Requirements: python3 (wave + math + struct modules, all stdlib).
"""

import argparse
import math
import struct
import sys
import wave
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent / "data"

FIXTURES = {}


def fixture(name, desc):
    """Decorator to register a fixture generator."""
    def wrapper(func):
        FIXTURES[name] = (func, desc)
        return func
    return wrapper


def write_wav(path, samples, sampwidth, framerate, nchannels):
    """Write raw samples to a WAV file.

    Samples must be a list of integers in the range appropriate for sampwidth.
    For multi-channel, samples are interleaved: [L0, R0, L1, R1, ...].
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    max_val = (1 << (8 * sampwidth - 1)) - 1
    min_val = -(1 << (8 * sampwidth - 1))

    with wave.open(str(path), "wb") as w:
        w.setnchannels(nchannels)
        w.setsampwidth(sampwidth)
        w.setframerate(framerate)
        w.setnframes(len(samples) // nchannels)

        fmt = {1: "b", 2: "h", 4: "i"}[sampwidth]
        raw = b""
        for s in samples:
            s = max(min_val, min(max_val, int(s)))
            raw += struct.pack(f"<{fmt}", s)
        w.writeframes(raw)


def sine_wave(freq, framerate, duration, sampwidth, nchannels, amplitude=None):
    """Generate a sine wave."""
    if amplitude is None:
        amplitude = (1 << (8 * sampwidth - 1)) - 1
    nframes = int(framerate * duration)
    nsamples = nframes * nchannels
    samples = []
    for i in range(nframes):
        t = i / framerate
        val = amplitude * math.sin(2 * math.pi * freq * t)
        for _ in range(nchannels):
            samples.append(val)
    return samples


def silence(duration, framerate, sampwidth, nchannels):
    """Generate silence."""
    nframes = int(framerate * duration)
    return [0] * (nframes * nchannels)


def white_noise(duration, framerate, sampwidth, nchannels, amplitude=None):
    """Generate white noise using a deterministic LCG."""
    if amplitude is None:
        amplitude = ((1 << (8 * sampwidth - 1)) - 1) // 8
    nframes = int(framerate * duration)
    nsamples = nframes * nchannels
    # Deterministic LCG (same seed every run)
    seed = 12345
    samples = []
    for i in range(nsamples):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        val = int(amplitude * ((seed / 0x7FFFFFFF) * 2 - 1))
        samples.append(val)
    return samples


def swept_sine(duration, framerate, sampwidth, nchannels, f0=20, f1=8000, amplitude=None):
    """Generate a linear frequency sweep."""
    if amplitude is None:
        amplitude = (1 << (8 * sampwidth - 1)) - 1
    nframes = int(framerate * duration)
    samples = []
    for i in range(nframes):
        t = i / framerate
        freq = f0 + (f1 - f0) * (t / duration)
        phase = 2 * math.pi * (f0 * t + 0.5 * (f1 - f0) * t * t / duration)
        val = amplitude * math.sin(phase)
        for _ in range(nchannels):
            samples.append(val)
    return samples


# ── Fixture definitions ──────────────────────────────────────────────────────

@fixture("sine_440_stereo_44100_16_2s.wav", "440 Hz sine, stereo 44.1 kHz 16-bit, 2 seconds")
def gen_sine_440_stereo():
    samples = sine_wave(440, 44100, 2.0, 2, 2)
    write_wav(DATA_DIR / "sine_440_stereo_44100_16_2s.wav", samples, 2, 44100, 2)


@fixture("sine_440_mono_44100_16_2s.wav", "440 Hz sine, mono 44.1 kHz 16-bit, 2 seconds")
def gen_sine_440_mono():
    samples = sine_wave(440, 44100, 2.0, 2, 1)
    write_wav(DATA_DIR / "sine_440_mono_44100_16_2s.wav", samples, 2, 44100, 1)


@fixture("sine_1000_mono_48000_16_1s.wav", "1 kHz sine, mono 48 kHz 16-bit, 1 second")
def gen_sine_1k_48k():
    samples = sine_wave(1000, 48000, 1.0, 2, 1)
    write_wav(DATA_DIR / "sine_1000_mono_48000_16_1s.wav", samples, 2, 48000, 1)


@fixture("sine_220_mono_22050_16_2s.wav", "220 Hz sine, mono 22.05 kHz 16-bit, 2 seconds")
def gen_sine_220_22k():
    samples = sine_wave(220, 22050, 2.0, 2, 1)
    write_wav(DATA_DIR / "sine_220_mono_22050_16_2s.wav", samples, 2, 22050, 1)


@fixture("sine_1000_mono_11025_16_2s.wav", "1 kHz sine, mono 11.025 kHz 16-bit, 2 seconds")
def gen_sine_1k_11k():
    samples = sine_wave(1000, 11025, 2.0, 2, 1)
    write_wav(DATA_DIR / "sine_1000_mono_11025_16_2s.wav", samples, 2, 11025, 1)


@fixture("sine_1000_mono_8000_16_2s.wav", "1 kHz sine, mono 8 kHz 16-bit, 2 seconds")
def gen_sine_1k_8k():
    samples = sine_wave(1000, 8000, 2.0, 2, 1)
    write_wav(DATA_DIR / "sine_1000_mono_8000_16_2s.wav", samples, 2, 8000, 1)


@fixture("silence_stereo_44100_16_0.5s.wav", "Silence, stereo 44.1 kHz 16-bit, 0.5 seconds")
def gen_silence_stereo():
    samples = silence(0.5, 44100, 2, 2)
    write_wav(DATA_DIR / "silence_stereo_44100_16_0.5s.wav", samples, 2, 44100, 2)


@fixture("silence_mono_44100_16_0.5s.wav", "Silence, mono 44.1 kHz 16-bit, 0.5 seconds")
def gen_silence_mono():
    samples = silence(0.5, 44100, 2, 1)
    write_wav(DATA_DIR / "silence_mono_44100_16_0.5s.wav", samples, 2, 44100, 1)


@fixture("noise_stereo_44100_16_3s.wav", "White noise, stereo 44.1 kHz 16-bit, 3 seconds")
def gen_noise_stereo():
    samples = white_noise(3.0, 44100, 2, 2)
    write_wav(DATA_DIR / "noise_stereo_44100_16_3s.wav", samples, 2, 44100, 2)


@fixture("swept_sine_stereo_44100_16_3s.wav", "Swept sine 20Hz->8kHz, stereo 44.1 kHz 16-bit, 3 seconds")
def gen_swept_sine():
    samples = swept_sine(3.0, 44100, 2, 2)
    write_wav(DATA_DIR / "swept_sine_stereo_44100_16_3s.wav", samples, 2, 44100, 2)


@fixture("sine_440_mono_44100_8_2s.wav", "440 Hz sine, mono 44.1 kHz 8-bit, 2 seconds")
def gen_sine_8bit():
    samples = sine_wave(440, 44100, 2.0, 1, 1, amplitude=100)
    write_wav(DATA_DIR / "sine_440_mono_44100_8_2s.wav", samples, 1, 44100, 1)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Generate LAME test audio fixtures")
    parser.add_argument("--force", "-f", action="store_true", help="Overwrite existing files")
    parser.add_argument("--list", "-l", action="store_true", help="List fixtures without generating")
    args = parser.parse_args()

    if args.list:
        for name, (func, desc) in FIXTURES.items():
            path = DATA_DIR / name
            status = "exists" if path.exists() else "missing"
            print(f"  {name:<50s} [{status}]  {desc}")
        return

    generated = 0
    skipped = 0
    for name, (func, desc) in sorted(FIXTURES.items()):
        path = DATA_DIR / name
        if path.exists() and not args.force:
            print(f"SKIP {name} (exists)")
            skipped += 1
            continue
        print(f"GEN  {name}  -- {desc}")
        func()
        generated += 1

    print(f"\nDone: {generated} generated, {skipped} skipped.")


if __name__ == "__main__":
    main()
