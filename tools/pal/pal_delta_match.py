"""Match USA functions without a PAL counterpart by neighbour offset.

For each USA address: take the offsets (pal - usa) of the nearest mapped
functions below and above, test usa+offset in the PAL tree and accept it when
the PAL function exists and its opcode sequence equals the USA one (100 %).
usage: pal_delta_match.py <usa_gen> <pal_gen> <usa_to_pal.tsv> ADDR...   -> TSV rows
"""
import bisect, glob, re, sys

def load(root):
    funcs = {}
    for f in glob.glob(root + '/gta4_recomp.*.cpp'):
        for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{(.*?)\n\}\n', open(f).read(), re.S):
            funcs[int(m.group(1), 16)] = re.findall(r'\n\t// ([a-z][a-z0-9.]*)', m.group(2))
    return funcs

U, P = load(sys.argv[1]), load(sys.argv[2])
M = {}
for l in open(sys.argv[3]).read().splitlines()[1:]:
    u, p, *_ = l.split('\t'); M[int(u, 16)] = int(p, 16)
keys = sorted(M); used = set(M.values())
for a in (int(x, 16) for x in sys.argv[4:]):
    if a in M or a not in U: continue
    i = bisect.bisect_left(keys, a)
    offs = []
    for k in keys[max(0, i - 2):i + 2]:
        d = M[k] - k
        if d not in offs: offs.append(d)
    for d in offs:
        p = a + d
        if p in P and p not in used and P[p] == U[a] and len(U[a]) >= 3:
            print('0x%08X\t0x%08X\tdelta\t%d' % (a, p, len(U[a]))); used.add(p); break
