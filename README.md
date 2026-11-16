# Arcanum CE — macOS fork (lu4nik)

> **This is a modified version of [Arcanum: Community Edition](https://github.com/alexbatalov/arcanum-ce)
> by Alexander Batalov.** It is distributed free of charge for non-commercial use under the same
> [Sustainable Use License](LICENSE.md). The changes below were made with the help of AI (Claude by
> Anthropic) for playing the game comfortably on a Mac, in particular with the
> [Ukrainian translation](https://github.com/lu4nik/arcanum-ua). Not affiliated with the upstream
> project; please do not report problems of this fork upstream.

## What is different from upstream

- **Movies play on macOS.** Bink videos (logos, intro, story cutscenes, endings) are decoded with a
  minimal static FFmpeg (only the Bink demuxer and decoders), with sound.
- **Sharp, filtered scaling.** The low-res frame is scaled to the screen without blur; optional
  edge-smoothing filters: xBR (CPU) and ScaleFX (GPU). See `Smooth` below.
- **Comfortable fullscreen.** Exclusive fullscreen: the menu bar no longer slides in at the top edge
  and steals the mouse while edge-scrolling; the cursor stays inside the game.
- **Two-finger trackpad panning** of the map (the wheel still scrolls lists over interface windows).
- **Ctrl+click** moves an item to the other side in loot, barter and steal windows (same as a right
  click, which is awkward on a trackpad).
- **Autosave** every few minutes into two rotating slots, never over the F7 quick save. See `AutoSave`.
- **Cyrillic text input** (cp1251): save names, character name and map notes can be typed in Ukrainian.
- **Premade characters** take their name from the translated level scheme.
- **Debug log** in `~/Library/Logs/arcanum-ce.log`, rewritten on every start.
- **Fixes:** texts with the Cyrillic letter «я» (cp1251 byte 0xFF) no longer cut off; UAP 2.x
  Educator's Guide no longer empties two shop inventories; duplicated "ghost" buttons after moving
  windows; black letterbox bars.

Tested on macOS (Apple Silicon) with the GOG version, Unofficial Arcanum Patch 2.0.2 and the Ukrainian
translation.

## Running it on a Mac

1. You need your own copy of the game (GOG or Steam). Extract the Windows game files, for example
   from the GOG installer with [innoextract](https://constexpr.org/innoextract/)
   (`innoextract setup_arcanum.exe`; if `-I app` gives nothing, extract everything) and put the
   game folder at, say, `/Applications/Arcanum`.
2. Download `arcanum-ce-macos-*.zip` from [Releases](../../releases) and copy its contents into the
   game folder: `Arcanum Community Edition.app`, `HighRes/config.ini`, `HighRes/shaders/`.
3. The app is not notarized by Apple: the first time, right-click it → **Open**, or run
   `xattr -dr com.apple.quarantine "/Applications/Arcanum/Arcanum Community Edition.app"`.
4. Optional: the Unofficial Arcanum Patch data files and a translation are installed into the same
   folder as on Windows.

## Settings (`HighRes/config.ini`)

| key | meaning |
|---|---|
| `Width`, `Height` | internal resolution, scaled to fill the screen (e.g. `1152`×`720` is exactly 3× on a 16" MacBook Pro) |
| `Smooth` | `0` plain pixels, `1` xBR, `2` xBRZ (personal builds only, otherwise xBR), `3` ScaleFX on the GPU. **F9** cycles in game |
| `SmoothUI` | `1` also smooth interface and text, `0` keep them as crisp pixels |
| `AutoSave` | autosave every N real minutes into two rotating slots, `0` off (default `10`) |
| `WheelScroll` | map panning speed for two-finger swipes, `0` off, negative inverts |
| `Renderer` | `0` OpenGL (default), `1` Metal |

## Building

```console
$ scripts/build_ffmpeg_bink.sh          # minimal FFmpeg for Bink -> out/ffmpeg-bink
$ cmake --preset macos -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
$ cmake --build --preset macos-release
```

`-DARCANUM_XBRZ=ON` adds the xBRZ filter. xBRZ is GPL-3, which is incompatible with redistributing it
inside this project, so it is off by default and the released app does not contain it; enable it only
for your own build.

## Third-party components

- [FFmpeg](https://ffmpeg.org) 7.1 (LGPL-2.1), statically linked, configured with only the Bink
  demuxer and decoders; `scripts/build_ffmpeg_bink.sh` rebuilds it so the app can be relinked.
- [ScaleFX](https://github.com/libretro/glsl-shaders/tree/master/scalefx) shaders by Sp00kyFox (MIT),
  in `first_party/tig/shaders/scalefx`.
- [xBRZ](https://sourceforge.net/projects/xbrz/) by Zenju (GPL-3), optional, not in released builds.

---

*The original README of Arcanum: Community Edition follows.*

# Arcanum Community Edition

> [!IMPORTANT]
> This is a beta, technology preview, or whatever other label you have in mind for a pre-release software.

## Status

The game is ready, but I haven't tested everything - just the speedrun - and that was about three months ago, so things might have changed since then.

Regarding the source code, about half of the modules (not half of the code) are in good shape. All APIs have meaningful names, along with brief documentation, annotations, and explanations (see `skill.c` and `quest.c` as examples). The other half may have cryptic names, little to no symbols , and no documentation at all (`anim.c` is extremely large and hard to understand).

## Installation

You must own the game to play. Purchase your copy on [GOG](https://www.gog.com/game/arcanum_of_steamworks_and_magick_obscura) or [Steam](https://store.steampowered.com/app/500810).

<details>
    <summary>Minimum installation</summary>

    ```
    .
    ├── arcanum1.dat
    ├── arcanum2.dat
    ├── arcanum3.dat
    ├── arcanum4.dat
    ├── modules
    │   ├── Arcanum
    │   │   ├── movies
    │   │   │   ├── 00069.bik
    │   │   │   ├── 01138.bik
    │   │   │   ├── 02112.bik
    │   │   │   ├── 50000.bik
    │   │   │   ├── 51169.bik
    │   │   │   ├── 91568.bik
    │   │   │   ├── A0021.bik
    │   │   │   ├── G0021.bik
    │   │   │   └── movies.mes
    │   │   └── sound
    │   │       └── music
    │   │           ├── Arcanum.mp3
    │   │           ├── Caladon.mp3
    │   │           ├── Caladon_Catacombs.mp3
    │   │           ├── Cities.mp3
    │   │           ├── Combat 1.mp3
    │   │           ├── Combat 2.mp3
    │   │           ├── Combat 3.mp3
    │   │           ├── Combat 4.mp3
    │   │           ├── Combat 5.mp3
    │   │           ├── Combat 6.mp3
    │   │           ├── CombatMusic.mp3
    │   │           ├── Dungeons.mp3
    │   │           ├── DwarvenMusic.mp3
    │   │           ├── Interlude.mp3
    │   │           ├── Isle_of_Despair.mp3
    │   │           ├── Kerghan.mp3
    │   │           ├── Mines.mp3
    │   │           ├── Qintara.mp3
    │   │           ├── Tarant.mp3
    │   │           ├── Tarant_Sewers.mp3
    │   │           ├── Towns.mp3
    │   │           ├── Tulla.mp3
    │   │           ├── Vendegoth.mp3
    │   │           ├── Villages.mp3
    │   │           ├── Void.mp3
    │   │           └── Wilderness.mp3
    │   ├── Arcanum.PATCH0
    │   ├── Arcanum.dat
    │   └── Vormantown.dat
    └── tig.dat
    ```
</details>

### Windows

Download and copy `arcanum-ce.exe` to your `Arcanum` folder. It serves as a drop-in replacement for `arcanum.exe`.

### Linux

- Use the Windows installation as a base - it contains the data assets needed to play. Copy the `Arcanum` folder somewhere, for example `/home/john/Desktop/Arcanum`.

- Alternatively, you can extract the required files from the GOG installer:

```console
$ sudo apt install innoextract
$ innoextract ~/Downloads/setup_arcanum.exe -I app
$ mv app Arcanum
```

- Download and copy `arcanum-ce` to this folder.

- Run `./arcanum-ce`.

### macOS

> [!NOTE]
> macOS 10.13 (High Sierra) or higher is required. Runs natively on Intel-based Macs and Apple Silicon.

- Use the Windows installation as a base - it contains the data assets needed to play. Copy the `Arcanum` folder somewhere, for example `/Applications/Arcanum`.

- Alternatively, if you have Homebrew installed, you can extract the required files from the GOG installer:

```console
$ brew install innoextract
$ innoextract ~/Downloads/setup_arcanum.exe -I app
$ mv app /Applications/Arcanum
```

- Download and copy `Arcanum Community Edition.app` to this folder.

- Run `Arcanum Community Edition.app`.

### Android & iOS

These ports are not currently intended for players. Touch controls are not yet implemented, and window management is subpar. No further instructions will be provided until these issues are resolved (but you can easily figure it out, it's not rocket science).

## Building from source

Check [`ci-build.yml`](.github/workflows/ci-build.yml) for details on how the project is compiled.

## Configuration

Several configuration options are available as command-line switches (admittedly not very user-friendly):

- `-4637`: Enable cheat level 3

- `-window`: Run in windowed mode (default is fullscreen)

- `-geometry=1280x720`: Set window size (default is 800x600)

When `HighRes/config.ini` from the Unofficial Arcanum Patch is present in the game directory, Community Edition also imports `Width`, `Height`, `Windowed`, `ShowFPS`, `ScrollFPS`, `ScrollDist`, `Logos`, and `Intro` at startup.

## Contributing

Play the game and file bugs if any (there are likely many). Attach a save game for investigation. Suggestions for quality of life improvements are also welcome. The major objective for 25H2 is to clarify remaining functions.

## License

The source code is this repository is available under the [Sustainable Use License](LICENSE.md).
