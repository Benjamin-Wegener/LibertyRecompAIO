#!/usr/bin/env python3
"""Derive a USA->PAL data/global address map from matched function pairs.

For every function pair in usa_to_pal.tsv with equal instruction count, track
`lis`-built base registers and record the effective addresses used by
D-form loads/stores/addi at the same instruction index. Votes per USA address.
Usage: pal_data_map.py <usa_gen_dir> <pal_gen_dir> <usa_to_pal.tsv> > data_map.tsv
"""
import collections, glob, re, sys

def funcs(d):
    out = {}
    for p in glob.glob(d + "/gta4_recomp.*.cpp"):
        cur = None; ins = []
        for l in open(p):
            m = re.match(r"DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)", l)
            if m:
                if cur: out[cur] = ins
                cur = int(m.group(1), 16); ins = []
            elif cur:
                mm = re.match(r"\t// ([a-z][a-z0-9_.+-]*)\s*(.*)$", l)
                if mm: ins.append((mm.group(1), mm.group(2).strip()))
        if cur: out[cur] = ins
    return out

DFORM = {"lwz","lbz","lhz","lha","stw","stb","sth","lfs","lfd","stfs","stfd","lwzu","stwu","lmw","stmw","ld","std"}

def eas(ins):
    reg = {}; res = {}
    for i, (op, args) in enumerate(ins):
        a = [x.strip() for x in re.split(r"[,()]", args) if x.strip()]
        try:
            if op == "lis" and len(a) == 2:
                reg[a[0]] = (int(a[1], 0) << 16) & 0xFFFFFFFF
            elif op == "addi" and len(a) == 3 and a[1] in reg:
                v = (reg[a[1]] + int(a[2], 0)) & 0xFFFFFFFF; res[i] = v; reg[a[0]] = v
            elif op == "ori" and len(a) == 3 and a[1] in reg:
                v = reg[a[1]] | int(a[2], 0); res[i] = v; reg[a[0]] = v
            elif op in DFORM and len(a) == 3 and a[2] in reg:
                res[i] = (reg[a[2]] + int(a[1], 0)) & 0xFFFFFFFF
                if a[0] in reg and not op.startswith("st"): del reg[a[0]]
            else:
                if a and op not in ("cmpwi","cmplwi","b","bl","blr","beq","bne","blt","bge","bgt","ble","stw","stb","sth"):
                    reg.pop(a[0], None)
        except ValueError:
            pass
    return res

def main():
    usa = funcs(sys.argv[1]); pal = funcs(sys.argv[2])
    votes = collections.defaultdict(collections.Counter)
    for l in open(sys.argv[3]).read().splitlines()[1:]:
        u, p, meth, _ = l.split("\t")
        if meth != "exact": continue
        ui, pi = usa.get(int(u, 16)), pal.get(int(p, 16))
        if not ui or not pi or len(ui) != len(pi): continue
        eu, ep = eas(ui), eas(pi)
        for k, v in eu.items():
            if k in ep and 0x82000000 <= v < 0x83200000:
                votes[v][ep[k]] += 1
    print("usa\tpal\tvotes\ttotal")
    for v in sorted(votes):
        top, n = votes[v].most_common(1)[0]
        print(f"0x{v:08X}\t0x{top:08X}\t{n}\t{sum(votes[v].values())}")

if __name__ == "__main__":
    main()
