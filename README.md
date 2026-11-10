# Liberty Recomp AIO

**Grand Theft Auto IV (Xbox 360) on Android, natively.** Static recompilation, no emulator.
One APK, both game versions (USA and Europe), every GPU family.

> [!WARNING]
> **Alpha, work in progress.** Expect bugs. You need your own copy of the game; this project
> contains no game data.

## At a glance

| | |
|---|---|
| Game versions | USA (with Title Update 8) and Europe (PAL), detected at install |
| Devices | arm64 Android 10+ with Vulkan 1.2 |
| GPUs | Adreno (bundled Mesa Turnip), Mali / Immortalis, others via the system driver |
| Controls | touch (adaptive layout or Xbox-style), USB and Bluetooth gamepads |
| Performance | presets for slow phones, per-feature switches, 30 FPS target |

## Install

1. Download the APK from the latest [Actions run](../../actions/workflows/build-android-apk.yml)
   ("Android APK" artifact) and install it.
2. Copy your game disc image (`.iso`) to the phone. The USA disc also needs Title Update 8.
3. Open the app, choose the files under **Game Files**, press **Install**, then **Play**.

The installer records the region and the launcher loads the matching executable:

| Disc | Needs | Executable |
|---|---|---|
| USA | Title Update 8 (0.0.8.5) | `libmain_usa.so`, the unmodified upstream recompilation |
| Europe (PAL) | nothing else | `libmain.so`, the PAL recompilation ([docs/PAL.md](docs/PAL.md)) |

## Launcher

* **Play**, **Game Files**, **Graphics & Controls**, **Overlay**, **Driver**, **About**.
* **Graphics & Controls** has four tabs: *Display* (resolution, upscaler, anti-aliasing, motion
  blur), *Performance* (frame limit, draw distance, population, shadows, reflections, audio
  buffer, CPU boost, heat guard), *Touch* (layout, size, opacity) and *Experimental*.
* A **preset slider** (Saver, Low, Medium, High, Ultra) sets the performance switches together
  for a 30 FPS target. Frame rates that do not divide the screen's refresh rate are hidden, so
  frames last equally long.
* The **Driver** page chooses Turnip (default on Adreno) or the system driver.

## Performance notes

* Pixel 9a (Mali) is CPU bound, Redmi 13 5G (Adreno 613) is GPU bound. Measurements, closed
  ideas and what is next: [docs/PERFORMANCE_ROADMAP.md](docs/PERFORMANCE_ROADMAP.md).
* **Frame generation** (Experimental tab) is alpha and not finished: the game renders every
  second image and the one in between is a plain blend of two frames. It ghosts on fast motion,
  adds about one frame of input delay and saved no measurable power on the Pixel 9a. Off by
  default. A motion-compensated version was built and dropped; the roadmap says why and what a
  good one would need.

## Status

| Area | State |
|---|---|
| Install, boot, menus, settings | works (PAL tested; USA uses the upstream code and is untested on Adreno) |
| 3D, cutscenes, driving | works |
| Gamepad (USB) and touch controls | work |
| Skip button on the intro logos (touch) | works |
| Vehicle headlights on walls and ground | missing (PAL, under investigation) |
| Vulkan 1.1 devices | planned ([issue](../../issues/8)) |
| Savegame export/import | planned |

## Build

CI builds both libraries and the APK ([`build-android-apk.yml`](.github/workflows/build-android-apk.yml)):
the PAL `libmain.so` from this tree and the USA `libmain_usa.so` from the pinned upstream commit in
a second build. Local builds: see [docs/BUILDING.md](docs/BUILDING.md); the Android scripts are in
`os/android/scripts/` (`build_native.sh`, `build_apk.sh`) and `tools/aio/build_usa_lib.sh` builds the
USA library for `jniLibs`.

## How PAL support works

The PAL `default.xex` is recompiled and the port's USA-keyed hooks are retargeted with generated
function, data, return-address and structure-offset maps (`tools/pal/`). See
[docs/PAL.md](docs/PAL.md).

## Credits

Based on:

* [Liberty Recompiled](https://github.com/OZORDI/LibertyRecomp) and the ReXGlue SDK
* [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid) by vaduur (Android port)
* [ZMOD-Liberty](https://github.com/prollynotzjay/ZMOD-Liberty) by prollynotzjay (Mali Edition);
  its README is kept as [docs/README-mali-edition.md](docs/README-mali-edition.md)

GTA IV is a trademark of Rockstar Games / Take-Two Interactive. Not affiliated with or endorsed by
them.
