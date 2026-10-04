# Dark Engine / Thief 2 runner

This repository restores the historical Dark Engine sources as a Visual
Studio 2022 project and builds a Thief 2 executable with a small game-data
runner and an in-game command console.

The repository does not include Thief 2 missions, movies, sounds, scripts, or
other retail assets. A legally obtained installation of *Thief II: The Metal
Age* is required to run the game.

## Current status

The Release/x86 solution builds successfully with the Visual Studio 2022 v143
toolset. The executable can use an external retail Thief 2 installation and
retains the standard startup sequence, mission loading, game UI, inventory,
and automap.

The runner currently provides:

- first-run selection and validation of a Thief 2 data directory;
- reuse of the selected directory on later launches;
- native display-resolution defaults when the user has not configured a
  resolution;
- the standard splash/startup, intro movie, and main-menu flow; and
- an in-game command console on an unused slash or backslash binding.

## Requirements

- 64-bit Windows 10 or Windows 11 capable of running 32-bit applications
- Visual Studio 2022 or Visual Studio 2022 Build Tools
- The **Desktop development with C++** workload, including:
  - MSVC v143 x86 build tools
  - a Windows 10 or Windows 11 SDK
  - MASM support installed with the C++ toolchain
- A retail Thief 2 installation containing `cam.cfg` and either `DARK.GAM` or
  `MISS1.MIS`

The legacy DirectX headers and import libraries used by the build are under
`3rdparty/dx7sdk`. The build target is 32-bit x86.

## Build instructions

### Visual Studio

1. Open `thief2.sln` in Visual Studio 2022.
2. Select the `Release` configuration and `x86` solution platform.
3. Build the solution with **Build > Build Solution**.
4. The resulting executable is `Release\Thief2.exe`.

`Directory.Build.props` selects the Thief 2 target by default and applies the
packing and C-language compatibility settings required by the legacy code.

### Developer Command Prompt

From an **x86 Native Tools Command Prompt for VS 2022**, run:

```bat
msbuild thief2.sln /m /t:Build /p:Configuration=Release /p:Platform=x86
```

The command should finish with `Release\Thief2.exe`.

## Run instructions

1. Start `Release\Thief2.exe`.
2. On the first launch, select the root of the Thief 2 installation. This is
   the directory containing `cam.cfg` and `DARK.GAM` or `MISS1.MIS`.
3. The runner changes the working directory to the selected installation and
   starts the engine. Retail data is read in place and is not copied into this
   repository.
4. The normal startup flow continues through the splash/startup screen, intro
   movie, and main menu.

The selected directory is stored for the current Windows user at:

```text
HKCU\Software\OpenDarkEngine\Thief2\GameDataPath
```

If the stored directory no longer contains valid game data, the runner asks
for a new directory. To choose a different directory while the old one is
still valid, remove the `GameDataPath` value with Registry Editor and launch
the executable again.

If `screen_size` or `game_screen_size` is already defined in `cam.cfg`, that
size is retained when the display driver supports it. Otherwise, the game
uses the closest supported mode to the native resolution of the display under
the mouse pointer at startup. Display modes reported by DirectDraw are
registered at runtime instead of being limited to the engine's original fixed
resolution table. Unsupported 24/32-bit NewDark settings are normalized to
the original renderer's 16-bit surface path.

The retail game writes `skip_intro` after the intro has played. Add
`always_play_intro` to `cam.cfg` to show the intro on every launch.

## In-game console

Press backslash (`\`) during a mission. If backslash already has a custom
binding, slash (`/`) is used when available. Existing user bindings are not
overwritten.

Available commands:

- `help [command]` - show built-in command help
- `openmission [number/shortname]` - start an installed mission
- `listmissions` - list installed missions and their short names
- `immunity [on/off]` - prevent player damage
- `flying [on/off]` - disable or restore player gravity
- `invisible [on/off]` - toggle the existing player-invisibility property
- `reticle [on/off]` - show or hide the center reticle
- `retinfo` - describe the object and mesh under the reticle
- `listscripts` - list loaded script classes
- `debugscripts [on/off]` - log dispatched script messages to
  `script-debug.log`

Long mission and script listings are appended to `thief-console.log`. Both log
files are ignored by Git.

## Screenshots

These captures were produced during local runtime verification with external
retail game data. The data shown in the captures is not included in the
repository.

### In-game menu

![Thief 2 in-game menu](docs/screenshots/in-game-menu.png)

### Gameplay and inventory HUD

![Thief 2 gameplay and inventory HUD](docs/screenshots/gameplay-inventory.png)

### Automap

![Thief 2 automap](docs/screenshots/automap.png)

## Known limitations and remaining work

- The x64 solution configurations are not supported or validated. The working
  target is Release/x86.
- The renderer and display stack still use legacy DirectX-era APIs. Runtime
  display-mode registration and native-resolution behavior need testing
  across more GPUs, high-DPI configurations, and ultrawide displays.
- The first-run picker and startup path need clean-machine testing against the
  common CD, GOG, and Steam directory layouts.
- There is no installer or redistributable package. The executable is run
  directly from the build output and uses separately installed retail data.
- The console overlay has no scrollback. Long mission and script listings are
  written to `thief-console.log`.
- `flying` disables gravity but does not disable world collision. It is not a
  noclip mode.
- `invisible` uses the engine's existing invisibility/render property. AI
  perception behavior has not been exhaustively verified for every mission.
- Keyboard-layout behavior for the slash and backslash console bindings needs
  broader testing on non-US layouts.
- The full build still emits numerous warnings inherited from the legacy
  source. They require separate review before warning levels can be tightened.
- Automated runtime tests are limited because licensed retail data cannot be
  included in the repository or continuous-integration environment.

## Provenance and asset policy

The Dark Engine was developed by Looking Glass Studios and powered Thief,
Thief II, and System Shock 2. This source history originates from the publicly
circulated historical source release. Review the applicable rights and
licenses before distributing binaries. Do not use this project to distribute
copyrighted game assets.
