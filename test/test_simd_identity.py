#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run_encode(lame: Path, input_wav: Path, label: str, simd: str, tmp: Path, extra_env=None) -> bytes:
    output = tmp / f"simd-{label}.mp3"
    env = os.environ.copy()
    env["LAMER_SIMD"] = simd
    if extra_env:
        env.update(extra_env)
    subprocess.run(
        [str(lame), "--quiet", "-V", "0", str(input_wav), str(output)],
        cwd=tmp,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    data = output.read_bytes()
    if not data:
        raise AssertionError(f"empty output for {label}")
    return data


def main() -> int:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} LAME INPUT_WAV [INPUT_WAV ...]", file=sys.stderr)
        return 2

    lame = Path(sys.argv[1]).resolve()
    input_wavs = [Path(arg).resolve() for arg in sys.argv[2:]]

    with tempfile.TemporaryDirectory(prefix="lamer-simd-identity-", dir=".") as td:
        tmp = Path(td)
        for input_wav in input_wavs:
            scalar = run_encode(lame, input_wav, "0", "0", tmp)
            cases = [
                ("auto", "auto", None),
                ("sse2", "sse2", None),
                ("avx2", "avx2", None),
                ("neon", "neon", None),
                ("auto-quant-exp", "auto", {"LAMER_SIMD_EXPERIMENTAL_QUANT": "1"}),
                ("sse2-quant-exp", "sse2", {"LAMER_SIMD_EXPERIMENTAL_QUANT": "1"}),
                ("avx2-quant-exp", "avx2", {"LAMER_SIMD_EXPERIMENTAL_QUANT": "1"}),
                ("neon-quant-exp", "neon", {"LAMER_SIMD_EXPERIMENTAL_QUANT": "1"}),
            ]
            for label, simd, extra_env in cases:
                candidate = run_encode(lame, input_wav, label, simd, tmp, extra_env)
                if candidate != scalar:
                    print(
                        f"{input_wav.name}: {label} changed MP3 output "
                        "relative to LAMER_SIMD=0",
                        file=sys.stderr,
                    )
                    return 1

    print("ok: SIMD identity matrix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
