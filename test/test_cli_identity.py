#!/usr/bin/env python3

import subprocess
import sys


def main() -> int:
    lame = sys.argv[1]
    out = subprocess.check_output([lame, "--version"], text=True, stderr=subprocess.STDOUT)
    first = out.splitlines()[0] if out.splitlines() else ""
    if not first.startswith("LAME "):
        print(f"unexpected version banner: {first!r}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
