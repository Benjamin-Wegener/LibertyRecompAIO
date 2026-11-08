#!/usr/bin/env python3
"""Re-target guarded USA hook blocks to PAL addresses and enable them.

Reads the ORIGINAL (USA, `#ifndef GTA4_PAL_SAFE_MODE`-guarded) source file and
rewrites it in one pass, so addresses are never translated twice.

* function map: usa_to_pal.tsv (exact/anchored)
* data map:     data_map.tsv from pal_data_map.py (top vote >= 60 % of votes)

A guarded block is enabled (`#ifndef GTA4_PAL_SAFE_MODE_RETARGETED`, never
defined) only if every 0x82/0x83 address and sub_ name inside it is mapped.
Outside blocks every mapped address is rewritten.

Usage: pal_retarget_hooks.py <usa_to_pal.tsv> <data_map.tsv> <orig-file> <out-file>
"""
import re
import sys

TOK = re.compile(r"(?<![0-9A-Fa-f])(sub_|0x)(8[23][0-9A-Fa-f]{6})(?=[uU]?\b)")


def load(fmap, dmap):
    F, D = {}, {}
    for line in open(fmap).read().splitlines()[1:]:
        u, p, meth, _ = line.split("\t")
        if meth in ("exact", "anchored", "delta", "manual"):
            F[int(u, 16)] = int(p, 16)
    for line in open(dmap).read().splitlines()[1:]:
        u, p, v, t = line.split("\t")
        if int(v) * 10 >= int(t) * 6:
            D[int(u, 16)] = int(p, 16)
    import os
    rm = os.path.join(os.path.dirname(dmap), 'return_map.tsv')
    if os.path.exists(rm):  # mid-function return addresses (tools/pal_map_return_addrs.py)
        for line in open(rm).read().splitlines()[1:]:
            u, p = line.split('\t')
            D[int(u, 16)] = int(p, 16)
    return F, D


def main():
    F, D = load(sys.argv[1], sys.argv[2])

    def tr(mo):
        a = int(mo.group(2), 16)
        if mo.group(1) == "sub_":
            v = F.get(a)
        else:
            v = F.get(a, D.get(a))
        return mo.group(0) if v is None else f"{mo.group(1)}{v:08X}"

    def mapped(a, kind):
        return a in F if kind == "sub_" else (a in F or a in D)

    lines = open(sys.argv[3]).read().split("\n")
    out, i, done, kept = [], 0, 0, 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("#ifndef GTA4_PAL_SAFE_MODE"):
            j = i
            while not lines[j].startswith("#endif"):
                j += 1
            text = "\n".join(lines[i + 1:j])
            ok = all(mapped(int(a, 16), k) for k, a in TOK.findall(text))
            if ok:
                out.append("#ifndef GTA4_PAL_SAFE_MODE_RETARGETED  // PAL-retargeted, see tools/pal_retarget_hooks.py")
                out.extend(TOK.sub(tr, text).split("\n"))
                out.append(lines[j])
                done += 1
            else:
                out.extend(lines[i:j + 1])
                kept += 1
            i = j + 1
            continue
        out.append(TOK.sub(tr, ln))
        i += 1
    open(sys.argv[4], "w").write("\n".join(out))
    print(f"retargeted={done} kept-disabled={kept}")


if __name__ == "__main__":
    main()
