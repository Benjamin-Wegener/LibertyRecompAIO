"""Find code addresses built with lis+addi/ori in the PAL code that are not registered
functions and do not lie inside an existing function.

A function's extent is the larger of its instruction count and the highest address it
is known to reach: branch labels (loc_X) and call return addresses (ctx.lr = X). Switch-table targets are
built with lis/addi too; they lie inside their function and must not be added
(declaring one splits the function: "Unresolved branch" at runtime).
Run it on a codegen output that does not yet contain the candidates.
usage: pal_find_code_consts.py <pal_generated_dir>   -> prints addresses"""
import bisect, glob, re, sys

extent = {}
consts = set()
for f in glob.glob(sys.argv[1] + '/gta4_recomp.*.cpp'):
    text = open(f).read()
    for m in re.finditer(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\) \{(.*?)\n\}\n', text, re.S):
        start, body = int(m.group(1), 16), m.group(2)
        reach = [int(x, 16) for x in re.findall(r'loc_([0-9A-F]{8})', body)]
        reach += [int(x, 16) for x in re.findall(r'ctx\.lr = 0x([0-9A-F]{8})', body)]
        ninstr = len(re.findall(r'\n\t// [a-z]', body))
        extent[start] = max([start + 4 * ninstr - 4] + reach)
        lis = {}
        for op, args in re.findall(r'\t// ([a-z.]+) ([^\n]*)', body):
            a = args.replace(' ', '').split(',')
            try:
                if op == 'lis':
                    lis[a[0]] = (int(a[1]) & 0xFFFF) << 16
                elif op in ('addi', 'ori') and a[1] in lis:
                    v = int(a[2])
                    consts.add((lis[a[1]] + v) & 0xFFFFFFFF if op == 'addi' else lis[a[1]] | (v & 0xFFFF))
            except (ValueError, IndexError):
                pass

starts = sorted(extent)
lo, hi = 0x82120000, 0x82A13D5C   # PAL code section
out = []
for c in sorted(consts):
    if not (lo <= c < hi) or c & 3 or c in extent:
        continue
    i = bisect.bisect_right(starts, c) - 1
    if i >= 0 and c <= extent[starts[i]]:
        continue
    out.append(c)
print(' '.join('%08X' % c for c in out))
