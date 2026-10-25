# MSC client integration review â€” 24 October 2026

## Source and precedence

- Local custom checkout started at `7a7ed9b` (MSC client 0.1.10), with no local edits.
- Reviewed official upstream `v2.1.2` (`97793b7`) and current main (`55a7342`). Current main includes the 2.1.2 changes and the protocol-55 required-plugin/presence updates used by the supplied server.
- Imported the supplied `MSC-ReSkate-Protocol55-Client-Build-Fix-Source.zip` as the port baseline, then reviewed and repaired the client integration against upstream and the original custom checkout.
- Archive documents were treated as descriptions of earlier work, not as new user instructions. Archive AGENTS.md and CLAUDE.md were not installed in this checkout.
- The entire `Server/` directory is byte-for-byte identical to the supplied archive. No server implementation was rewritten. Shared client/server protocol code retains protocol 55 and the separate MSC packet 38.
- The user reported that their default ReSkate 2.1.2 client connects to their updated server. This is user-reported validation; the merged client still needs its own live test.

## Client corrections

- Restored upstream plugin ticking/settings, plugin chat, required-plugin checks, input presence reporting and presence reset on disconnect.
- Recovered MSC runtime adapters dropped by the web port: first-person settings, FOV handling, controller/keyboard input, camera callbacks, spectator handoff, trick samples, scoring-expression filters, physics observations and terminology/trainer entry points.
- Kept upstream renderer listeners and plugin board-pose processing. MSC spectator refinement runs through its own final callback.
- Terminology now shares upstream's existing HUD writer. It no longer depends on the obsolete native Hall of Meat hook. Plugin HUD requests and terminology retain independent ownership of the shared detour.
- Preserved upstream's plugin architecture for Hall of Meat, Road Rash, Board Break, drone and grind-exit add-ons. Their old native C++ copies remain as reference source but are not reactivated alongside the upstream plugin implementations. Steezy Landings remains a native MSC feature.
- Ordinary throwdown queues and plugin invitations use upstream wire layouts. Enhanced SKATE data is negotiated between capable participants; later acknowledgements update capability without duplicating queue membership. Existing MSC and upstream relay tests both remain enabled.
- Fixed missing UI declarations and the malformed first-person settings panel. The MSC Hub, rules, terminology editor and first-person controls are connected to the upstream menu.
- Restored MSC trick logger, SKATE rules and terminology test targets removed from the archive build configuration.

## Local validation

- Windows x64 MSVC v145 Release: launcher and runtime compiled and linked with `/W4 /WX`.
- 28 selected regression suites passed: 17 first-person/camera suites; protocol-55 transport, multiplayer protocol, throwdown relay, plugins, trick logger, SKATE rules and terminology; launcher mod manager, Thunderstore, package validation and start maps.
- Test output: `build/protocol55-firstperson-regression-tests.log`.
- Build-system limitation: MSBuild reports MSB8064 for directory dependencies `assets` and `assets/emotes`. Both directories and the embedded assets exist. These are incremental-build dependency warnings, not C++ warnings or missing packaged content.
- Source whitespace check: `git diff --check` passed.
- Nothing was pushed, released or installed into the game directory.

## Reproduce and test

```powershell
./build-client.ps1 -RunTests
./package-client.ps1 -SkipBuild
```

The dedicated `msc-protocol55-local` preset stamps ReSkate 2.1.2 with mod identity MSC/0.1.10, uses the custom repository identity and disables binary auto-updates. Output is `build/protocol55-local/Release/`. The local ZIP contains `ReSkateLauncher.exe`, `ReSkate.dll`, and the bundled Recommended first-person preset under `scripts/Custom/FirstPersonNative/presets/bundled/`.

Before release, test this merged client in the actual game: connect/reconnect to the updated MSC server; check required-plugin/map installation and plugin menus; use MSC Hub; verify first-person toggles, controls, spectator switching and camera restoration; check terminology; and play both MSC-only and mixed stock/MSC SKATE matches. Compilation and host tests do not establish native hook or multiplayer behavior in the running game.

## First-person correction after game testing

The earlier build compiled but omitted `first_person_integration::start_when_ready()` after overlay initialization. The code and settings files were present, yet the first-person settings worker and camera callbacks were never activated. The earlier host tests exercised the modules independently and did not establish that this game entry point was wired. That earlier ZIP should be replaced by the FirstPersonFix build.

The call is restored immediately after `s.ready = true`, as in the original custom build. The complete original `first_person_controls` function is also restored, including the Advanced sections, per-value resets, FOV reset, profiles, directional glances, on-board/off-board right-stick configuration, bail/get-up comfort and collision settings, camera cycling, spectator option and spring controls. All 62 tracked first-person module/support files were compared against original custom commit `7a7ed9b`; they match after line-ending normalization. Existing host-specific native adapters retain their MSC logic with the upstream APIs around them.

The audit also recovered the custom practice trick-feedback overlay. It now renders alongside upstream's plugin and screen-capture UI, including when the main menu is closed. An audit of namespaced API calls added by the custom branch found only one deliberate replacement: the old Hall of Meat publisher coordination now uses the shared upstream HUD writer, as described above. This source audit is a check of custom additions; it is not evidence that every game hook or upstream plugin behaves identically in live play.

Additional regression targets restored from the custom build: `first_person_settings_host`, `first_person_native_fov`, and `first_person_camera_host`. All 28 selected suites pass. Final compiler output is in `build/protocol55-firstperson-build.log`.

The server source remains byte-for-byte identical to the supplied server port. No binaries were installed into the game directory and nothing was pushed. First-person readiness and these features must now be confirmed in the game; after the overlay initializes the log should contain `Native First Person settings, glance and right-stick camera connected after overlay readiness.`

## Authorized release — 25 October 2026

The user reported the corrected client tested and working and authorized publishing main and both client/server releases. Release versions are MSC client/launcher 0.1.11 and dedicated server 2.1.5. Release binaries enable updates exclusively from Jish2637/ReSkateMerge; the separate local-validation client preset keeps updates disabled. Server implementation is the supplied protocol-55 port; version stamping is controlled by CMake.

The release server configuration test required renaming a shadowed local variable under MSVC /WX. This changes only the test; server implementation is preserved.
