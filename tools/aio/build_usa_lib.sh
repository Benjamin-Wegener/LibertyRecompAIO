#!/bin/bash
# Build libmain_usa.so (USA disc + Title Update 8) for Liberty Recomp AIO.
#
# The USA executable is the unmodified upstream recompilation: the first commit
# of this repository ("Import ZMOD-Liberty ...") is that state. This script
# checks it out into a separate worktree, builds only libmain.so there and copies
# it to the Android jniLibs as libmain_usa.so. Build the normal (PAL) libmain.so
# as usual; the launcher picks the library from LibertyRecomp/region.
#
# usage: tools/aio/build_usa_lib.sh [worktree-dir]   (needs ANDROID_NDK)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
TREE="${1:-$REPO/../liberty-usa-tree}"
BASE="$(git -C "$REPO" rev-list --max-parents=0 HEAD | tail -1)"
: "${ANDROID_NDK:?set ANDROID_NDK to the Android NDK}"

if [[ ! -d "$TREE" ]]; then
  git -C "$REPO" worktree add --detach "$TREE" "$BASE"
  git -C "$TREE" submodule update --init --recursive
fi
# Build fixes that are not game code: 16 KB page alignment, macOS DXC crash.
for f in toolchains/android.cmake \
         LibertyRecompLib/shader_overrides/bink/ps_bink.hlsl \
         LibertyRecompLib/shader_overrides/bink/ps_bink_no_alpha.hlsl \
         glue/rexglue-sdk-main/tools/gta4_shader_override_compiler.py; do
  cp "$REPO/$f" "$TREE/$f"
done

cmake -S "$TREE" -B "$TREE/build-android" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/android.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_NDK="$ANDROID_NDK" \
  -DLIBERTY_RECOMP_TARGET_PLATFORM=android -DLIBERTY_RECOMP_VULKAN=ON -DLIBERTY_RECOMP_METAL=OFF \
  -DLIBERTY_ANDROID_THINLTO=ON -DLIBERTY_ANDROID_RENDERER_THINLTO=ON \
  -DLIBERTY_ANDROID_FUSED_VMADDFP=ON -DLIBERTY_RECOMP_ANDROID_RUNTIME_ASSETS=ON
ninja -C "$TREE/build-android" glue/gta4-recomp/libmain.so

JNI="$REPO/os/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNI"
cp -f "$TREE/build-android/glue/gta4-recomp/libmain.so" "$JNI/libmain_usa.so"
echo "libmain_usa.so -> $JNI"
