"""Append shaders from a XenosRecomp translation to the checked-in shader cache.

Used for PAL: shaders whose container hash is not in the USA cache are dumped from
the device (gta4_containers/), translated with XenosRecomp (GTA4 mode, no AIR/DXIL)
and appended here. Entries stay sorted by hash (the runtime uses lower_bound).

usage: pal_add_shaders.py <cache.cpp> <new translation cache.cpp> <out cache.cpp>
"""
import re, sys
import zstandard

ENTRY = re.compile(r'^\t\{ (0x[0-9A-F]+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), '
                   r'"([^"]*)", nullptr, (\d+) \},\s*$')

def load(path):
    text = open(path, encoding="utf-8").read()
    ents = []
    for line in text.split("\n"):
        m = ENTRY.match(line)
        if m:
            g = m.groups()
            ents.append(dict(hash=int(g[0], 16), dxil=(int(g[1]), int(g[2])), spirv=(int(g[3]), int(g[4])),
                             late=(int(g[5]), int(g[6])), air=(int(g[7]), int(g[8])), spec=int(g[9]),
                             filename=g[10], tex=int(g[11])))
    raw = bytes(int(x) for x in re.search(r"g_compressedSpirvCache\[\] = \{([^}]*)\};", text).group(1).split(",") if x.strip())
    size = int(re.search(r"g_spirvCacheDecompressedSize = (\d+);", text).group(1))
    return text, ents, zstandard.ZstdDecompressor().decompress(raw, max_output_size=size)

bt, base, bs = load(sys.argv[1]); _, new, ns = load(sys.argv[2])
have = {e["hash"] for e in base}
rows = [(e, bs) for e in base]
added = 0
for e in new:
    if e["hash"] in have: continue
    ext = "vert" if e["filename"].endswith(".vert") else "frag"
    e = dict(e, filename="shader/rage_shaders/runtime_captured/gta4_%016X.container.%s" % (e["hash"], ext))
    rows.append((e, ns)); added += 1
rows.sort(key=lambda r: r[0]["hash"])
spirv = bytearray(); lines = []
for e, blob in rows:
    main = blob[e["spirv"][0]:e["spirv"][0] + e["spirv"][1]]; late = blob[e["late"][0]:e["late"][0] + e["late"][1]]
    mo = len(spirv) if main else 0; spirv += main
    lo = len(spirv) if late else 0; spirv += late
    lines.append('\t{ 0x%X, %d, %d, %d, %d, %d, %d, %d, %d, %d, "%s", nullptr, %d },' %
                 (e["hash"], e["dxil"][0], e["dxil"][1], mo, len(main), lo, len(late), e["air"][0], e["air"][1], e["spec"], e["filename"], e["tex"]))
comp = zstandard.ZstdCompressor(level=19).compress(bytes(spirv))
t = bt
s = t.index("ShaderCacheEntry g_shaderCacheEntries[] = {\n") + len("ShaderCacheEntry g_shaderCacheEntries[] = {\n")
t = t[:s] + "\n".join(lines) + t[t.index("\n};", s):]
t = re.sub(r"const uint8_t g_compressedSpirvCache\[\] = \{[^}]*\};", lambda _: "const uint8_t g_compressedSpirvCache[] = {%s};" % ",".join(map(str, comp)), t)
t = re.sub(r"g_spirvCacheCompressedSize = \d+;", "g_spirvCacheCompressedSize = %d;" % len(comp), t)
t = re.sub(r"g_spirvCacheDecompressedSize = \d+;", "g_spirvCacheDecompressedSize = %d;" % len(spirv), t)
t = re.sub(r"g_shaderCacheEntryCount = \d+;", "g_shaderCacheEntryCount = %d;" % len(rows), t)
open(sys.argv[3], "w", encoding="utf-8").write(t)
print("added", added, "total", len(rows))
