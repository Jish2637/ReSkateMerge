# ReSkateMerge

Combined Windows x64 client: **skate-trick-detection-fixes → jish-fpm-native →
Terminology test27**, based on ReSkate 2.1.0. Source and releases are maintained
in the private [ReSkateMerge repository](https://github.com/Jish2637/ReSkateMerge).

## Use the combined client

1. Download `ReSkateMerge-v0.1.0-merged.1-Windows-x64.zip` from this repository's
   private release and extract it into a new folder of its own, outside Steam's
   skate. install. You need your own Steam copy of skate.
2. Run `ReSkateLauncher.exe` and let it prepare the supported game build. For an
   existing ReSkate folder, close the game, back up the launcher and DLL, then
   replace them and copy the bundled `scripts` directory into that folder.
3. This launcher is compiled with automatic ReSkate replacement disabled. Keep
   **Install ReSkate updates** off; install future combined builds manually.
4. Open **INS > Skater > First Person** for the native first-person options.
   Configure bindings under **Settings > Controls > Camera**. Remove the separate
   Jish First Person DLL before using the integrated version.
5. Open **INS > Sandbox > Map & HUD > HUD > Skate trick terminology** and its
   editor to enable custom names and combos. Existing schema 5 rules remain
   compatible at `%LOCALAPPDATA%\ReSkate\trainer\trick-terminology-editor.json`.

Detection and multiplayer matching retain native trick identities even when
terminology changes their displayed names. The bundled recommended first-person
preset is under `scripts/Custom/FirstPersonNative/presets/bundled/`.

## Build and verify

Install Visual Studio 2026 with Desktop development with C++, the v145 toolset,
a Windows SDK, and its bundled CMake (verified with CMake 4.3.1). From Developer
PowerShell in the repository:

```powershell
cmake --preset vs2026-x64
cmake --build --preset merged-release --parallel 4
ctest --test-dir build/vs2026-x64 -C Release --output-on-failure --output-junit "$PWD/build/vs2026-x64/test-results.xml"
```

Outputs are in `build/vs2026-x64/Release/`. The preset enables detection,
terminology, first-person, launcher, multiplayer, trainer and Hall of Meat tests.
Manual audio diagnostic probes are excluded from the default build; they have
incomplete upstream link dependencies and are not CTest tests. Existing VS 2022
and Linux presets are retained.

Package a clean committed build with:

```powershell
python contrib/merge/package_client.py
```

See [source provenance](contrib/merge/PROVENANCE.md) and
[validation](contrib/merge/VALIDATION.md). Automated checks do not establish live
camera behavior, native HUD refresh, controller navigation, persistence across
game launches or multiplayer ownership; those require an in-game playtest.

## First-person feature reference

JishFPMNative integrates Jish's First Person Mod into ReSkate's existing INS menu. It replaces ReSkate.dll and uses ReSkate's native camera settings, controls, and menu instead of a separate First Person panel.

Note: This is a Fork of ReSkate and NOT a Standalone Mod.

## Features

- First-person camera settings under **INS > Skater > First Person**, using ReSkate's window or full-screen menu.
- Off-board right-stick look and on-board free look with a held modifier, adjustable sensitivity, limits, smoothing, and auto-centering.
- Optional held skating-stick input while using on-board free look.
- Directional glances, toggle glances, and optional glance restoration when returning to a marker.
- First-person spectator view, third-person bail/get-up options, and camera collision during bails.
- First Person profiles, Reset to defaults, and individual slider Reset buttons.
- First Person bindings in **Settings > Controls > Camera**, unbound by default.

## Credits

Built from [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) and [Jish's First Person Mod](https://github.com/Jish2637/JishsFirstPersonMod), with native integration and adjustments maintained here.
