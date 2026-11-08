# Debug notes 2026-11-08: black screen, missing headlight cones, build pitfalls

## Results (what is verified and what is not)

| Build | Source | Picture | Headlight cone | Note |
|---|---|---|---|---|
| CI release `v0.6.0-alpha.1.7` | commit `1e0e9ff0`, CI run 42511364858 | ok | seen by the user | known-good reference |
| local, native libs of alpha.1.7 + launcher of `1e0e9ff0` (versionCode 28) | same | ok ("läuft") | to confirm | installed on the Pixel 9a |
| local, **really compiled** `9f94ab7c` (versionCode 27) | commit `9f94ab7c` | **black** | - | scene and minimap content black |
| local, full branch head `a01a5cdf` (versionCode 21) | `a01a5cdf` | **black** | - | |

The black screen therefore comes from `9f94ab7c` ("retarget PAL globals ...", 56 changed lines in
`gta4_native_hooks.cpp`: deferred depth/GBuffer globals moved by a constant offset, clear colors forced to 0)
or from something it brings along. `a01a5cdf` (shadow skip, six lines, only active for
`gta4_native_shadow_interval > 1`) is on top of it and is not the cause on its own.
Not yet isolated: which hunk of `9f94ab7c` is responsible.

Evidence that the constant-offset retarget in `9f94ab7c` is suspicious: the verified neighbours in
`tools/pal/maps/data_map.tsv` shift by different amounts per region (for example `-0x76c68` around
`0x8310cfa0`), not by `-0x10000` / `-0x140000`. These addresses are not in the map, so they cannot be
verified with it.

## The cone is visible in the logs

Lines `sub_822EA320 [point-spot]` and `sub_822EDA58 [shaft]` (added in `1e0e9ff0`) are printed the first
ten times a spot light is drawn. Logs 022 to 029 (19:46 to 20:50) contain them (the cone worked); logs
030 and later (from 20:52) never do. 20:52 is exactly when builds switched from the `aio-repo` build
to builds from `work/fork` with the `.aio` package.

## `stencil-resolve-unrepresentable` is not the cause

The log line `gta4-native-cause: point=depth-stencil-resolve result=rejected
reason=stencil-resolve-unrepresentable` and `explicit-depth-handoff ... source-unwritten` appear in
thousands in the black runs. They were **added as diagnostics in `1e0e9ff0`**, so their absence in older
logs proves nothing and they are not the cause by themselves.

## Build pitfalls that produced wrong test results

1. **`git archive <commit> | tar -x` gives every file the commit time.** `ninja` then sees sources older
   than the object files and compiles nothing (`native build: 0 s`). Three "bisect" APKs (versionCode 22,
   24, 26) therefore contained the identical old `libmain.so`. After extracting a commit, `touch` the
   changed sources, and check the build log for `Building CXX object` / a non-zero `native build:` time.
2. **`work/fork` has parts of its build directory under version control** (about 1,400 files of
   `build-android` in the fork repository). A branch switch removes or restores them. Do not run
   `git add -A` in `work/fork`, and restore with `git checkout <branch> -- build-android`.
3. **Two build trees.** `tools/local_build.sh` builds `work/fork`; the commits live in `work/aio-repo`.
   Fixes applied to both trees can drift apart (the fork has its own history, `d3e4eac4`, `a44f0a83`).
4. **Android refuses downgrades.** A build with a lower `versionCode` cannot be installed over a higher
   one, and uninstalling deletes `Android/data/<pkg>/files` (about 7 GB of game data). Always raise
   `versionCode` for a test build. Current device: 28.
5. **Never install while the game runs**, and never build with a full disk: at 100 % even the shell output
   file cannot be written.

## Ways to get a known-good native build quickly

- Download the CI release APK (`gh release download v0.6.0-alpha.1.7 -R Benjamin-Wegener/LibertyRecompAIO`),
  copy `lib/arm64-v8a/*.so` into `os/android/app/src/main/jniLibs/arm64-v8a/`, and run only
  `./gradlew assembleRelease -PlibertyGpu=mali` with a raised `versionCode`. This gives the exact CI
  binaries in the `.aio` package without the 2-hour native build.

## Performance numbers recorded the same day (Pixel 9a, PAL, 540p, FSR off)

Street scene, three cars: 17-19 fps before the CPU fixes, 22-24 fps after (the phone was hotter, thermal
status 3), 27-28 fps with the cooler phone and `gta4_native_shadow_interval=2` plus
`gta4_native_water_reflection_interval=2`. See issue #2 and PR #1 for the profile (58 % of CPU time is guest
game code, about 25 % the renderer).
