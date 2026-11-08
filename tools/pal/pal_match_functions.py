#!/usr/bin/env python3
"""Match recompiled functions between the USA (1.00+TU8) and PAL generated
trees by their PPC instruction sequence (taken from the disassembly comments
rexglue emits above each statement).

Immediates, branch targets and addresses are masked, so relocated code with
different data/call addresses still matches. Only matches whose normalized
fingerprint is unique on both sides are reported as exact; a second pass keeps
the opcode skeleton only and accepts it when it is unique and the neighbouring
functions agree on the address delta.

Output: TSV  usa_addr  pal_addr  method  ninsns
Usage:  pal_match_functions.py <usa_generated_dir> <pal_generated_dir> > map.tsv
"""
import hashlib
import re
import sys
from collections import defaultdict
from pathlib import Path

DEF = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
INS = re.compile(r"^\s*//\s+([a-z][a-z0-9_.+-]*)\s*(.*)$")
NUM = re.compile(r"-?0x[0-9a-fA-F]+|-?\b\d+\b")
REG_OK = re.compile(r"^(r|f|v|cr)\d+$")


def norm_operands(ops: str) -> str:
    parts = []
    for tok in re.split(r"([,()])", ops.replace(" ", "")):
        if not tok or tok in ",()":
            parts.append(tok)
        elif REG_OK.match(tok):
            parts.append(tok)
        else:
            parts.append(NUM.sub("#", tok))
    return "".join(parts)


def parse_tree(d: Path, names_to_addr):
    funcs = {}
    for f in sorted(d.glob("*_recomp.*.cpp")):
        cur = None
        seq = []
        with f.open(errors="replace") as fh:
            for line in fh:
                m = DEF.match(line)
                if m:
                    if cur is not None:
                        funcs[cur] = seq
                    name = m.group(1)
                    cur = names_to_addr.get(name)
                    seq = []
                    continue
                if cur is None:
                    continue
                mi = INS.match(line)
                if mi:
                    seq.append((mi.group(1), norm_operands(mi.group(2))))
        if cur is not None:
            funcs[cur] = seq
    return funcs


def name_map(d: Path):
    m = {}
    reg = re.compile(r"SetFunction\(0x([0-9A-Fa-f]+),\s*(\w+)\)")
    for line in (d / next(p.name for p in d.glob("*_register.cpp"))).open():
        r = reg.search(line)
        if r:
            m[r.group(2)] = int(r.group(1), 16)
    return m


def fp(seq, full=True):
    h = hashlib.sha1()
    for mn, ops in seq:
        h.update((mn + " " + ops if full else mn).encode())
        h.update(b"\n")
    return h.hexdigest()


def unique_index(funcs, full):
    idx = defaultdict(list)
    for a, s in funcs.items():
        if len(s) >= 3:
            idx[fp(s, full)].append(a)
    return {k: v[0] for k, v in idx.items() if len(v) == 1}


def main():
    usa_dir, pal_dir = Path(sys.argv[1]), Path(sys.argv[2])
    usa = parse_tree(usa_dir, name_map(usa_dir))
    pal = parse_tree(pal_dir, name_map(pal_dir))
    print(f"# usa_funcs={len(usa)} pal_funcs={len(pal)}", file=sys.stderr)

    result = {}
    for full, method in ((True, "exact"), (False, "skeleton")):
        ui, pi = unique_index(usa, full), unique_index(pal, full)
        used = set(result.values())
        for k, ua in ui.items():
            if ua in result:
                continue
            pa = pi.get(k)
            if pa is not None and pa not in used:
                result[ua] = (pa, method)
        result = {u: v for u, v in result.items()}
        # flatten for 'used' bookkeeping
        result_addr = {u: v[0] for u, v in result.items()}
        result = {u: v for u, v in result.items()}
        used = set(result_addr.values())

    # neighbour-delta check for skeleton matches
    exact = sorted((u, v[0]) for u, v in result.items() if v[1] == "exact")
    import bisect
    eu = [u for u, _ in exact]
    out = []
    for u, (p, method) in sorted(result.items()):
        if method == "skeleton":
            i = bisect.bisect_left(eu, u)
            deltas = {exact[j][1] - exact[j][0] for j in range(max(0, i - 2), min(len(exact), i + 2))}
            if (p - u) not in deltas:
                method = "skeleton-unconfirmed"
        out.append((u, p, method, len(usa[u])))

    counts = defaultdict(int)
    for *_, m, _n in out:
        counts[m] += 1
    print("# " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())), file=sys.stderr)
    print("usa\tpal\tmethod\tninsns")
    for u, p, m, n in out:
        print(f"0x{u:08X}\t0x{p:08X}\t{m}\t{n}")


if __name__ == "__main__":
    main()
