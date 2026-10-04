# Dark Engine / Thief 2 runner

This repository restores the historical Dark Engine sources as a modern
Visual Studio project and provides a small Thief 2 runner. It does not contain
or redistribute Thief 2 game data; you must own a copy of *Thief II: The Metal
Age*.

## Running Thief 2

Launch `Thief2.exe`. On first run, select the Thief 2 installation directory
that contains `cam.cfg` and `DARK.GAM` (or `MISS1.MIS`). The runner stores that
directory for the current Windows user under
`HKCU\Software\OpenDarkEngine\Thief2` and reuses it on later launches.

Unless `screen_size` or `game_screen_size` is set explicitly in `cam.cfg`, the
game starts at the native resolution of the display under the mouse pointer.
The original startup flow is preserved: splash/startup, intro movie, and main
menu. As in the retail game, the intro is marked as seen after it plays; add
`always_play_intro` to `cam.cfg` to play it on every launch.

## In-game console

Press backslash (`\`) during a mission. If backslash already has a custom
binding, slash (`/`) is used when available. Existing user bindings are never
overwritten.

Commands:

- `help [command]` — show built-in command help
- `openmission [number/shortname]` — start an installed mission
- `listmissions` — list installed missions and their short names
- `immunity [on/off]` — prevent player damage
- `flying [on/off]` — toggle gravity-free player movement
- `invisible [on/off]` — toggle player invisibility
- `reticle [on/off]` — show or hide the center reticle
- `retinfo` — describe the object or mesh under the reticle
- `listscripts` — list loaded script classes
- `debugscripts [on/off]` — log dispatched script messages to
  `script-debug.log`

Long mission and script listings are also appended to `thief-console.log`.
Both logs are ignored by Git.

## Building

Open `thief2.sln` in Visual Studio 2022 and build the Release/x86
configuration. `Directory.Build.props` selects the Thief 2 build and keeps the
legacy project defaults in one place.

## Provenance

The Dark Engine was developed by Looking Glass Studios and powered Thief,
Thief II, and System Shock 2. This source history originates from the publicly
circulated historical source release. Review the applicable rights and
licenses before distributing binaries, and do not use this project to
distribute copyrighted game assets.
