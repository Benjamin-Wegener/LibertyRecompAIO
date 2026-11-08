"""Map USA mid-function return addresses (ctx.lr values) to PAL.

For each address A in the given list: find the USA function F containing
`ctx.lr = 0xA;` and the callee C called right after; take the k-th call of C in F,
map F and C with usa_to_pal.tsv and return the lr of the k-th call of C' in F'.
usage: pal_map_return_addrs.py <usa_gen> <pal_gen> <usa_to_pal.tsv> ADDR...
"""
import glob, re, sys

def load(root):
    funcs = {}
    for f in glob.glob(root + '/gta4_recomp.*.cpp'):
        t = open(f).read()
        for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{(.*?)\n\}\n', t, re.S):
            funcs[int(m.group(1), 16)] = [(lr, 'sub_%08X' % int(tgt, 16)) for tgt, lr in re.findall(
                r'// bl 0x([0-9a-f]+)\s*\n\s*ctx\.lr = 0x([0-9A-F]{8});', m.group(2))]
    return funcs

U, P = load(sys.argv[1]), load(sys.argv[2])
M = {}
for l in open(sys.argv[3]).read().splitlines()[1:]:
    u, p, *_ = l.split('\t'); M[int(u, 16)] = int(p, 16)
def norm(c):
    m = re.match(r'sub_([0-9A-F]{8})', c)
    return M.get(int(m.group(1), 16)) if m else c
for a in (int(x, 16) for x in sys.argv[4:]):
    hit = None
    for f, calls in U.items():
        for i, (lr, callee) in enumerate(calls):
            if int(lr, 16) == a:
                hit = (f, i, callee); break
        if hit: break
    if not hit: print(f'{a:08X} not-found'); continue
    f, i, callee = hit
    k = sum(1 for lr, c in U[f][:i] if c == callee)
    pf = M.get(f); pc = norm(callee)
    if pf is None or pf not in P: print(f'{a:08X} func {f:08X} unmapped'); continue
    cands = [lr for lr, c in P[pf] if (('sub_%08X' % pc) if isinstance(pc, int) else pc) == c]
    print(f'{a:08X} -> {cands[k] if k < len(cands) else "?"}  (usa func {f:08X} -> {pf:08X}, callee {callee} #{k})')
