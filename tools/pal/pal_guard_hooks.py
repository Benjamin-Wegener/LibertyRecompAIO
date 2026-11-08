#!/usr/bin/env python3
"""Wrap every USA-address hook override in the port sources with
`#ifndef GTA4_PAL_SAFE_MODE ... #endif`.

The port overrides generated guest functions by defining strong
`extern "C" void sub_82XXXXXX(PPCContext& ctx, uint8_t* base) { ... }`
symbols. Those addresses come from the USA 1.00 + TU8 executable; in the PAL
executable they are missing or belong to different functions (docs/06 §3.5).
With GTA4_PAL_SAFE_MODE defined, the weak generated thunk is used instead,
so the PAL code runs unhooked.

Idempotent: already-guarded definitions are skipped.
Usage: pal_guard_hooks.py <gta4-recomp/src> [--dry-run]
"""
import re
import sys
from pathlib import Path

GUARD = "GTA4_PAL_SAFE_MODE"
START = re.compile(r'^(?:extern\s+"C"\s+void\s+|REX_HOOK_RAW\(\s*)(sub_82[0-9A-Fa-f]{6})\b')
ONE_LINE = re.compile(r'^\s*ASPECT_(?:HUD|DC)_HOOK\((sub_82[0-9A-Fa-f]{6})\)\s*$')
SKIP_FILES = {"gta4_pal_stubs.cpp", "gta4_pal_initterm_scan.cpp"}


def find_body_end(lines, i):
    """Return index of the line containing the brace closing the definition
    that starts at line i, or None if it is only a declaration."""
    depth = 0
    seen_open = False
    in_block_comment = False
    for j in range(i, len(lines)):
        s = lines[j]
        k = 0
        in_str = None
        while k < len(s):
            c = s[k]
            nxt = s[k + 1] if k + 1 < len(s) else ""
            if in_block_comment:
                if c == "*" and nxt == "/":
                    in_block_comment = False
                    k += 2
                    continue
                k += 1
                continue
            if in_str:
                if c == "\\":
                    k += 2
                    continue
                if c == in_str:
                    in_str = None
                k += 1
                continue
            if c == "/" and nxt == "/":
                break
            if c == "/" and nxt == "*":
                in_block_comment = True
                k += 2
                continue
            if c in "\"'":
                in_str = c
            elif c == ";" and not seen_open and depth == 0:
                return None  # prototype only
            elif c == "{":
                depth += 1
                seen_open = True
            elif c == "}":
                depth -= 1
                if seen_open and depth == 0:
                    return j
            k += 1
    return None


def process(path: Path, dry: bool):
    lines = path.read_text().splitlines(keepends=True)
    out = []
    i = 0
    n = 0
    while i < len(lines):
        prev = lines[i - 1] if i > 0 else ""
        o = ONE_LINE.match(lines[i])
        if o and f"#ifndef {GUARD}" not in prev:
            out.append(f"#ifndef {GUARD}\n{lines[i]}#endif  // {GUARD} {o.group(1)}\n")
            i += 1
            n += 1
            continue
        m = START.match(lines[i])
        if m and f"#ifndef {GUARD}" not in prev:
            end = find_body_end(lines, i)
            if end is not None:
                out.append(f"#ifndef {GUARD}  // USA address, see tools/pal_guard_hooks.py\n")
                out.extend(lines[i:end + 1])
                out.append(f"#endif  // {GUARD} {m.group(1)}\n")
                i = end + 1
                n += 1
                continue
        out.append(lines[i])
        i += 1
    if n and not dry:
        path.write_text("".join(out))
    return n


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    src = Path(sys.argv[1])
    dry = "--dry-run" in sys.argv
    total = 0
    for p in sorted(src.rglob("*")):
        if p.suffix not in {".cpp", ".inc", ".h", ".mm"} or p.name in SKIP_FILES:
            continue
        n = process(p, dry)
        if n:
            print(f"{n:4d}  {p.relative_to(src)}")
            total += n
    print(f"guarded={total}{' (dry run)' if dry else ''}")


if __name__ == "__main__":
    main()
