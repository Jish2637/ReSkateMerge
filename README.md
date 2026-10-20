# ReSkateMerge

Combined Windows x64 client: **skate-trick-detection-fixes → jish-fpm-native →
Terminology test27**, based on ReSkate 2.1.0. Source and releases are maintained
in the public [ReSkateMerge repository](https://github.com/Jish2637/ReSkateMerge).

## Use the combined client

1. Download `ReSkateMerge-v0.1.0-merged.6-Windows-x64.zip` from this repository's
   release and extract it into a new folder of its own, outside Steam's
   skate. install. You need your own Steam copy of skate.
2. Run `ReSkateLauncher.exe` and let it prepare the supported game build. For an
   existing ReSkate folder, close the game, back up the launcher and DLL, then
   replace them and copy the bundled `scripts` directory into that folder.
3. This launcher updates exclusively from **Jish2637/ReSkateMerge**. Keep
   **Install ReSkate updates** enabled to receive combined builds; a saved OFF
   choice stays off. The launcher offers updates through its usual controls.
   **Test pre-releases** opts into preview releases; `--no-update` disables
   updates for that launch. Offline checks keep the installed client usable.
4. Open **INS > Skater > First Person** for the native first-person options.
   Configure bindings under **Settings > Controls > Camera**. Remove the separate
   Jish First Person DLL before using the integrated version.
5. Open **INS > Sandbox > Map & HUD > HUD > Skate trick terminology** and its
   editor to enable custom names and combos. Existing schema 5 rules remain
   compatible at `%LOCALAPPDATA%\ReSkate\trainer\trick-terminology-editor.json`.

Detection and multiplayer matching retain native trick identities even when
terminology changes their displayed names. The bundled recommended first-person
preset is under `scripts/Custom/FirstPersonNative/presets/bundled/`.

`v0.1.0-merged.3` fixes USB PS5 input capture when the game reads multiple reports
at once: opening Insert or chat must neutralize each report without generating
a D-pad press. It also prevents Steam's HID descriptor wrapper from recursively
re-entering metadata discovery, which caused a startup stack overflow in merged.2.
See [controller fix](contrib/merge/CONTROLLER_FIX.md).

`v0.1.0-merged.4` clears active S.K.A.T.E. trick restrictions on match completion,
quitting, host closure, disconnects and world/session teardown. Saved setup options
remain available for the next match; native trick handling resumes outside it.

`v0.1.0-merged.5` adds the selected First Person Mod 1.36.2 features: optional
native head movement during on-foot camera-only right-stick look, and glance /
first-person entry/exit emote protection. Under **INS > Skater > First Person >
Advanced > Off-board right-stick look**, enable **Native head movement** with
on-foot right-stick look enabled and **Camera only** selected. It defaults off,
saves in settings and personal presets, and changes camera feel. Camera and
character mode and on-board look retain their existing input behavior.

Bound L1/LB + D-pad glances suppress the game's emote on the first press; a
deliberate double tap within 250 ms while holding L1/LB forwards the emote.
The first-person toggle always suppresses its D-pad emote when entering or
leaving first person, including when the shoulders are released before D-pad.
Unrelated controls and the physical shortcut reader remain available.

`v0.1.0-merged.6` enables public GitHub launcher updates for this combined client.
Install this ZIP manually once: merged.5 and earlier have updates compiled off.
Future normal releases supply `launcher.json`, `ReSkateLauncher.exe` and
`ReSkate.dll`, pinned by size and SHA-256. The launcher restarts after updating
itself and leaves game files, saved preferences, mods and personal presets alone.
It does not fall back to original upstream ReSkate binaries. The bundled
recommended preset is unchanged from merged.5.

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

The packager also generates `artifacts/launcher-v<VERSION>.json` and checksums
for the ZIP, manifest and binaries. It refuses a dirty tree, failed required
tests, stale source metadata or a different compiled update repository. Game
and DepotDownloader pins are retained in `contrib/merge/launcher-base.json`.
For future releases, write `artifacts/RELEASE_NOTES-v<VERSION>.md`, push the
committed source, then run `python contrib/merge/publish_release.py prepare
<VERSION>` and `python contrib/merge/publish_release.py publish <VERSION>`.
Preparation uploads and verifies a draft; publication makes it a normal latest
release. The one-time visibility change uses `make-public <VERSION>` after the
reviewed history audit and prepared assets, before initial publication.

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
