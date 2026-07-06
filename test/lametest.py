#!/usr/bin/env python3
"""LAME encoder test suite.

Modes:
  encode       Encode a WAV to MP3 and verify the output is valid MP3.
  stream       Encode via stdin/stdout pipe and verify.
  matrix       Run a matrix of encoding options from a .op file.
  roundtrip    Encode to MP3, decode back to WAV with ffmpeg, verify WAV
               properties match (channels, sample rate, non-zero length).
  formats      Encode multiple WAV files with varied input formats (sample
               rate, channels, bit depth) using a default setting.
"""

import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

MP3_SYNC_MASK = 0xE0
MP3_SYNC_VALUE = 0xE0


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(cmd, cwd, stdin=None, stdout=None):
    text_mode = stdout is None
    proc = subprocess.run(
        [str(part) for part in cmd],
        cwd=cwd,
        stdin=stdin,
        stdout=stdout if stdout is not None else subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=text_mode,
    )
    if proc.returncode != 0:
        sys.stderr.write(f"command failed ({proc.returncode}): {' '.join(map(str, cmd))}\n")
        if proc.stdout:
            sys.stderr.write(proc.stdout if text_mode else proc.stdout.decode('utf-8', errors='replace'))
        if proc.stderr:
            sys.stderr.write(proc.stderr if text_mode else proc.stderr.decode('utf-8', errors='replace'))
        raise SystemExit(proc.returncode)
    return proc


def ffmpeg_probe(path):
    """Return parsed ffprobe output as a dict, or None on failure."""
    try:
        proc = subprocess.run(
            ["ffprobe", "-v", "quiet", "-print_format", "json",
             "-show_format", "-show_streams", str(path)],
            capture_output=True, text=True, timeout=30,
        )
        if proc.returncode != 0:
            return None
        return json.loads(proc.stdout)
    except (FileNotFoundError, subprocess.TimeoutExpired, json.JSONDecodeError):
        return None


def looks_like_mp3(path):
    with path.open('rb') as handle:
        prefix = handle.read(3)
        if prefix == b'ID3':
            return True
        if len(prefix) < 2:
            return False
        return prefix[0] == 0xFF and (prefix[1] & MP3_SYNC_MASK) == MP3_SYNC_VALUE


def require_mp3_file(path, label):
    require(path.is_file(), f"missing {label}: {path}")
    require(path.stat().st_size > 0, f"empty {label}: {path}")
    require(looks_like_mp3(path), f"invalid mp3 output for {label}: {path}")


def wav_properties(path):
    """Return (channels, sample_rate, duration_seconds) for a WAV file using ffprobe."""
    info = ffmpeg_probe(path)
    if info is None:
        return None
    for stream in info.get("streams", []):
        if stream.get("codec_type") == "audio":
            ch = stream.get("channels", 0)
            sr = int(stream.get("sample_rate", 0))
            dur = float(stream.get("duration", 0))
            return (ch, sr, dur)
    return None


def load_cases(path):
    cases = []
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        cases.append(line)
    require(cases, f"no test cases found in {path}")
    return cases


def discover_wavs(directory):
    """Return sorted list of WAV files in a directory."""
    p = Path(directory)
    wavs = sorted(p.glob("*.wav"))
    require(wavs, f"no .wav files found in {directory}")
    return wavs


# ── Test modes ────────────────────────────────────────────────────────────────

def run_encode(lame, input_wav):
    with tempfile.TemporaryDirectory(prefix='lametest-encode-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        output = tmp / 'fixture.mp3'
        run([lame, '--quiet', '-b', '128', input_wav, output], cwd=tmp)
        require_mp3_file(output, 'encoded fixture')
    print('ok: encode fixture')


def run_stream(lame, input_wav):
    with tempfile.TemporaryDirectory(prefix='lametest-stream-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        output = tmp / 'stream.mp3'
        with input_wav.open('rb') as src, output.open('wb') as dst:
            run([lame, '--quiet', '-', '-'], cwd=tmp, stdin=src, stdout=dst)
        require_mp3_file(output, 'stream output')
    print('ok: stdin/stdout')


def run_matrix(lame, input_wav, options_file):
    cases = load_cases(options_file)
    with tempfile.TemporaryDirectory(prefix='lametest-matrix-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        for index, case in enumerate(cases, start=1):
            output = tmp / f"case-{index:03d}.mp3"
            run([lame, '--quiet', *shlex.split(case), input_wav, output], cwd=tmp)
            require_mp3_file(output, f"matrix output {index}")
    print(f"ok: {options_file.name} ({len(cases)} cases)")


def run_roundtrip(lame, input_wav, tolerance=0.05):
    """Encode WAV to MP3, decode with ffmpeg, verify properties match."""
    src_props = wav_properties(input_wav)
    require(src_props is not None, f"cannot probe input WAV: {input_wav}")

    with tempfile.TemporaryDirectory(prefix='lametest-roundtrip-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        mp3file = tmp / 'roundtrip.mp3'
        wavfile = tmp / 'roundtrip-decoded.wav'

        # Encode
        run([lame, '--quiet', '-b', '128', input_wav, mp3file], cwd=tmp)
        require_mp3_file(mp3file, 'roundtrip encoded')

        # Decode with ffmpeg
        run(["ffmpeg", "-v", "error", "-i", mp3file, "-y", wavfile], cwd=tmp)

        # Verify decoded WAV
        dec_props = wav_properties(wavfile)
        require(dec_props is not None, f"cannot probe decoded WAV: {wavfile}")
        require(dec_props[0] == src_props[0],
                f"channel mismatch: src={src_props[0]} dec={dec_props[0]}")
        require(dec_props[1] == src_props[1],
                f"sample rate mismatch: src={src_props[1]} dec={dec_props[1]}")
        dur_diff = abs(dec_props[2] - src_props[2])
        require(dur_diff < tolerance,
                f"duration mismatch: src={src_props[2]:.3f}s dec={dec_props[2]:.3f}s diff={dur_diff:.3f}s")

    print(f"ok: roundtrip {input_wav.name}")


def run_formats(lame, data_dir):
    """Encode every WAV in the data directory with a standard setting."""
    wavs = discover_wavs(data_dir)
    with tempfile.TemporaryDirectory(prefix='lametest-formats-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        for wav in wavs:
            output = tmp / f"{wav.stem}.mp3"
            run([lame, '--quiet', '-V', '4', wav, output], cwd=tmp)
            require_mp3_file(output, f"formats: {wav.name}")
    print(f"ok: formats ({len(wavs)} files)")


def run_decode_check(lame, input_wav):
    """Encode, then verify ffmpeg can decode the result without error."""
    with tempfile.TemporaryDirectory(prefix='lametest-decode-', dir='.') as tmpdir:
        tmp = Path(tmpdir)
        mp3file = tmp / 'decode.mp3'
        nullfile = tmp / 'null.wav'

        run([lame, '--quiet', '-b', '128', input_wav, mp3file], cwd=tmp)
        require_mp3_file(mp3file, 'decode-check encoded')

        # ffmpeg decode to null
        run(["ffmpeg", "-v", "error", "-i", mp3file, "-f", "null", "-"], cwd=tmp)
    print("ok: decode check")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="LAME encoder test suite")
    parser.add_argument('mode', choices=['encode', 'stream', 'matrix',
                                         'roundtrip', 'formats', 'decode'])
    parser.add_argument('--lame', required=True, help='Path to lame executable')
    parser.add_argument('--input', help='Path to input WAV file')
    parser.add_argument('--data-dir', help='Directory of WAV files (for formats mode)')
    parser.add_argument('--options', help='Path to .op file (for matrix mode)')
    args = parser.parse_args()

    lame = Path(args.lame).resolve()
    require(lame.is_file(), f"missing lame executable: {lame}")

    if args.mode == 'encode':
        require(args.input is not None, '--input is required for encode mode')
        run_encode(lame, Path(args.input).resolve())

    elif args.mode == 'stream':
        require(args.input is not None, '--input is required for stream mode')
        run_stream(lame, Path(args.input).resolve())

    elif args.mode == 'matrix':
        require(args.input is not None, '--input is required for matrix mode')
        require(args.options is not None, '--options is required for matrix mode')
        run_matrix(lame, Path(args.input).resolve(), Path(args.options).resolve())

    elif args.mode == 'roundtrip':
        require(args.input is not None, '--input is required for roundtrip mode')
        run_roundtrip(lame, Path(args.input).resolve())

    elif args.mode == 'formats':
        require(args.data_dir is not None, '--data-dir is required for formats mode')
        run_formats(lame, Path(args.data_dir).resolve())

    elif args.mode == 'decode':
        require(args.input is not None, '--input is required for decode mode')
        run_decode_check(lame, Path(args.input).resolve())


if __name__ == '__main__':
    main()
