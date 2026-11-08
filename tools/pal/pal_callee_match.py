"""Match changed USA functions to PAL by their call sequence.

For each USA address: the PAL candidates are functions whose ordered list of
called functions equals the USA list mapped through usa_to_pal.tsv (all callees
must be mapped, at least 2) and whose length is within 15 %. Printed only when
exactly one candidate fits.
usage: pal_callee_match.py <usa_gen> <pal_gen> <usa_to_pal.tsv> ADDR...  -> TSV rows
"""
import glob, re, sys

def load(root):
    funcs = {}
    for f in glob.glob(root + '/gta4_recomp.*.cpp'):
        for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{(.*?)\n\}\n', open(f).read(), re.S):
            body = m.group(2)
            funcs[int(m.group(1), 16)] = (len(re.findall(r'\n\t// [a-z]', body)),
                                          [int(t, 16) for t in re.findall(r'// bl 0x([0-9a-f]+)', body)])
    return funcs

U, P = load(sys.argv[1]), load(sys.argv[2])
M = {}
for l in open(sys.argv[3]).read().splitlines()[1:]:
    u, p, *_ = l.split('\t'); M[int(u, 16)] = int(p, 16)
used = set(M.values())
by_calls = {}
for a, (n, calls) in P.items():
    by_calls.setdefault(tuple(calls), []).append((a, n))
for a in (int(x, 16) for x in sys.argv[4:]):
    if a in M or a not in U: continue
    n, calls = U[a]
    mapped = [M.get(c) for c in calls]
    if len(calls) < 2 or None in mapped: continue
    cands = [(p, pn) for p, pn in by_calls.get(tuple(mapped), [])
             if p not in used and abs(pn - n) <= max(3, n * 0.15)]
    if len(cands) == 1:
        print('0x%08X\t0x%08X\tcallee\t%d' % (a, cands[0][0], n))
