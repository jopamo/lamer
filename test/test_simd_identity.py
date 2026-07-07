#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run_encode(lame: Path, input_wav: Path, simd: str, tmp: Path) -> bytes:
    output = tmp / f"simd-{simd}.mp3"
    env = os.environ.copy()
    env["LAMER_SIMD"] = simd
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
        raise AssertionError(f"empty output for LAMER_SIMD={simd}")
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
            scalar = run_encode(lame, input_wav, "0", tmp)
            for simd in ("auto", "sse2", "avx2", "neon"):
                candidate = run_encode(lame, input_wav, simd, tmp)
                if candidate != scalar:
                    print(
                        f"{input_wav.name}: LAMER_SIMD={simd} changed MP3 output "
                        "relative to LAMER_SIMD=0",
                        file=sys.stderr,
                    )
                    return 1

    print("ok: SIMD identity matrix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
