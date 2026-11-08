# Liberty Recomp AIO

> [!WARNING]
> **Alpha, work in progress.** Expect bugs.

**Liberty Recomp AIO** ("all in one") runs **Grand Theft Auto IV (Xbox 360)** natively on arm64
Android through static recompilation - no emulator. One APK for every GPU:

| GPU | Driver | Status |
|---|---|---|
| Qualcomm Adreno 6xx/7xx | bundled Mesa Turnip (default) or system driver | supported (from the original port) |
| ARM Mali / Immortalis | system Vulkan driver | tested on Pixel 8a and Pixel 9a (Tensor, Mali-G715) |
| Samsung Xclipse, Imagination PowerVR, others | system Vulkan driver (Vulkan 1.2 needed) | untested |

The app picks the driver at startup: Turnip on Adreno, the system driver everywhere else. It can
be changed under **Driver** on the launcher.

## Game version

Both discs are supported; the installer detects the region:

| Disc | Needs | Executable |
|---|---|---|
| USA | Title Update 8 (0.0.8.5) | `libmain_usa.so` - the unmodified upstream recompilation |
| Europe (PAL) | nothing else | `libmain.so` - PAL recompilation ([docs/PAL.md](docs/PAL.md)) |

You need your own copy of the game as a disc image (`.iso`), plus Title Update 8 for the USA disc.
Copy them to the phone, open the app, choose them under **Game Files** and press **Install**. The
installer writes the region to `LibertyRecomp/region`, and the launcher loads the matching
executable.

Building: the normal build produces the PAL `libmain.so`; `tools/aio/build_usa_lib.sh` builds
`libmain_usa.so` from the first commit of this repository (the upstream state) and puts it next to
it in `jniLibs`.

## Status

| Area | State |
|---|---|
| Install, boot, menus, settings | works (PAL tested; USA uses the upstream code, untested here) |
| 3D, cutscenes, driving | works |
| Gamepad (USB) and touch controls | work |
| Skip button on the intro logos (touch) | works |
| Vehicle headlights on walls/ground | missing (PAL, under investigation) |
| Savegame export/import | planned |

## How PAL support works

See [docs/PAL.md](docs/PAL.md): the PAL `default.xex` is recompiled, and the port's USA-keyed hooks
are retargeted with generated function, data, return-address and structure-offset maps
(`tools/pal/`).

## Credits

Liberty Recomp AIO is based on:

- [Liberty Recompiled](https://github.com/OZORDI/LibertyRecomp) and the ReXGlue SDK
- [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid) by vaduur (Android port)
- [ZMOD-Liberty](https://github.com/prollynotzjay/ZMOD-Liberty) by prollynotzjay (Mali Edition);
  its README is kept as [docs/README-mali-edition.md](docs/README-mali-edition.md)

GTA IV is a trademark of Rockstar Games / Take-Two Interactive. This project contains no game data;
you must own the game.
