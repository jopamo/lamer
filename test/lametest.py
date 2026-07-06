#!/usr/bin/env python3

import argparse
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


def load_cases(path):
    cases = []
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        cases.append(line)
    require(cases, f"no test cases found in {path}")
    return cases


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['encode', 'stream', 'matrix'])
    parser.add_argument('--lame', required=True)
    parser.add_argument('--input', required=True)
    parser.add_argument('--options')
    args = parser.parse_args()

    lame = Path(args.lame).resolve()
    input_path = Path(args.input).resolve()

    require(lame.is_file(), f"missing lame executable: {lame}")
    require(input_path.is_file(), f"missing input: {input_path}")

    if args.mode == 'encode':
        run_encode(lame, input_path)
    elif args.mode == 'stream':
        run_stream(lame, input_path)
    else:
        require(args.options is not None, '--options is required for matrix mode')
        run_matrix(lame, input_path, Path(args.options).resolve())


if __name__ == '__main__':
    main()
