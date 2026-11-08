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

This build runs the **European (PAL) disc** (`default.xex` 0.0.0.6, no title update needed).
USA disc support (1.00 + Title Update 8) side by side with PAL is planned.

You need your own copy of the game as a disc image (`.iso`). Copy it to the phone, open the app,
choose it under **Game Files** and press **Install**.

## Status (PAL)

| Area | State |
|---|---|
| Install, boot, menus, settings | works |
| 3D, cutscenes, driving | works |
| Gamepad (USB) and touch controls | work |
| Vehicle headlights on walls/ground | missing (under investigation) |
| Skip intro logos | in progress |
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
