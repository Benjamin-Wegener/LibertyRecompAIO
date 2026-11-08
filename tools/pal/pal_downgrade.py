#!/usr/bin/env python3
"""Convert rexglue 0.10.x codegen output to the 0.9.0 file layout.

The port vendors rexglue-sdk 0.9.0 (see glue/rexglue-sdk-main/CMakeLists.txt),
but v0.9.0 has no macOS codegen binary. v0.10.1 does, and its emitted function
bodies are structurally identical; only the header layout differs:

  0.9.0                         0.10.x
  ----------------------------  ----------------------------------------
  generated/gta4_init.h         generated/gta4_pch.h   (config + SDK)
                                generated/gta4_funcs.h (all DECLARE_REX_FUNC)
  gta4_recomp.N.cpp includes    includes "gta4_funcs.N.h"
    "gta4_init.h"
  PPCImageConfig has no         PPCImageConfig has .codegen_flags{...}

This script rebuilds the 0.9.0 layout so the output can be dropped into the
port's generated/ directory unchanged.

usage: pal_downgrade.py <0.10 generated dir> <output dir>
"""
import glob
import os
import re
import shutil
import sys


def main() -> int:
    if len(sys.argv) != 3:
        return print(__doc__) or 2
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)

    pch = open(os.path.join(src, "gta4_pch.h"), encoding="utf-8").read()
    funcs = open(os.path.join(src, "gta4_funcs.h"), encoding="utf-8").read()

    # --- gta4_init.h = pch + all function declarations -------------------
    decl_lines = [ln for ln in funcs.splitlines()
                  if ln.startswith("DECLARE_REX_FUNC(")]
    if not decl_lines:
        sys.exit("gta4_funcs.h has no declarations")
    init_h = pch.rstrip("\n") + "\n\n" + "\n".join(decl_lines) + "\n"
    with open(os.path.join(dst, "gta4_init.h"), "w", encoding="utf-8", newline="") as f:
        f.write(init_h)
    print(f"gta4_init.h: {len(decl_lines)} declarations")

    # --- gta4_init.cpp: drop the .codegen_flags block --------------------
    init_cpp = open(os.path.join(src, "gta4_init.cpp"), encoding="utf-8").read()
    init_cpp2, n = re.subn(
        r"\n\s*\.codegen_flags = \{(?:[^{}]|\{[^{}]*\})*\},\n", "\n", init_cpp)
    if n != 1:
        sys.exit(f"expected one .codegen_flags block in gta4_init.cpp, found {n}")
    with open(os.path.join(dst, "gta4_init.cpp"), "w", encoding="utf-8", newline="") as f:
        f.write(init_cpp2)
    print("gta4_init.cpp: removed .codegen_flags")

    # --- register.cpp, sources.cmake, rexglue.cmake: copy ----------------
    for name in ("gta4_register.cpp", "sources.cmake", "rexglue.cmake"):
        path = os.path.join(src, name)
        if os.path.exists(path):
            shutil.copyfile(path, os.path.join(dst, name))

    # --- recomp files: include the single header -------------------------
    files = sorted(glob.glob(os.path.join(src, "gta4_recomp.*.cpp")),
                   key=lambda p: int(re.search(r"\.(\d+)\.cpp$", p).group(1)))
    for path in files:
        text = open(path, encoding="utf-8").read()
        text2, n = re.subn(r'#include "gta4_funcs\.\d+\.h"',
                           '#include "gta4_init.h"', text)
        if n != 1:
            sys.exit(f"{os.path.basename(path)}: expected one funcs include, found {n}")
        with open(os.path.join(dst, os.path.basename(path)), "w",
                  encoding="utf-8", newline="") as f:
            f.write(text2)
    print(f"gta4_recomp.*.cpp: {len(files)} files rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
