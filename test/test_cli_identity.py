#!/usr/bin/env python3

import re
import subprocess
import sys


def main() -> int:
    lame = sys.argv[1]
    for args in (("--version",), ("--help",)):
        out = subprocess.check_output([lame, *args], text=True, stderr=subprocess.STDOUT)
        first = out.splitlines()[0] if out.splitlines() else ""
        if not re.match(
            r"^lamer version (?:[0-9a-f]{7}|unknown) "
            r"\(snapshot (?:\d{4}-\d{2}-\d{2}|unknown)\)$",
            first,
        ):
            print(f"unexpected version banner: {first!r}", file=sys.stderr)
            return 1
        if "3.101" in out or "lame.sourceforge.io" in out:
            print("legacy LAME version identity leaked into CLI output", file=sys.stderr)
            return 1
        if "LAME-compatible MP3 encoder (https://github.com/jopamo/lamer)" not in out:
            print("missing lamer project identity in CLI output", file=sys.stderr)
            return 1

        if args == ("--help",) and "usage: lame [options]" not in out:
            print("help output exposed the build path instead of the command name", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
