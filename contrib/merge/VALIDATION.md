# ReSkateMerge v0.1.0-merged.3 validation

Built on Windows x64 with Visual Studio 2026, MSVC 19.51.36247, Windows SDK
10.0.26100.0 and CMake 4.3.1. Release build with the `vs2026-x64` /
`merged-release` presets succeeded. Launcher automatic binary updates are
compiled off; the multiplayer protocol remains 52.

CTest: **104 passed, 0 failed, 1 skipped**, out of 105 selected tests. The
installed-game-only `trainer_tuning_names` check skips because
`DINGOSDK_TEST_GAME_ROOT` is unset. Set that CMake cache path to a supported game
installation to run it; invalid explicitly configured game data remains a failure.
Other tests that optionally read game assets exercised their synthetic fixtures.

Coverage includes:

- A fake Steam HID descriptor callback re-enters the attributes observer: both
  original attributes calls execute, only one descriptor query runs, the
  independent reader stays bypassed and exception unwinding restores the gate.
- USB DualSense/Edge batched reads (including the reported 192 bytes / three
  64-byte reports into a 2048-byte buffer), neutral D-pad/buttons/triggers,
  USB and Bluetooth layouts, counters, CRC updates, guard bytes, malformed
  transfers and idempotent completion processing. See CONTROLLER_FIX.md.
- Native detector fixtures, stance/body rotation, native filter and logger,
  localized identities, replay/capture handling and throwdown matching.
- Terminology test27: native label retention, pending prewrite publication,
  held rotation, Capture Last, combos, rule serialization and restoration.
- An integration regression checks detector signatures survive arbitrary custom
  names and blanked combo-member labels and that native revisions supersede aliases.
- First-person camera, transitions, FOV, native controls, sticks, glances,
  collision, comfort settings, profile queue and shared settings.
- Launcher mod management, multiplayer/mesh/protocol/relay, trainer contracts,
  Hall of Meat and related existing regression suites.

Release ZIP helper tests: 5 passed, 2 platform-specific cases skipped. Packaging
validates the archive, required feature-test report, Windows x64 PE headers and
disabled launcher updates. `BUILD_INFO.json` records the final source commit and
both binary checksums; `SHA256SUMS.txt` records the ZIP checksum. Import-table
inspection found only Windows system DLL dependencies for the launcher/runtime.
The first-person recommended preset and dependency license notices are included.

Build integration adjustments: updated mesh-test stubs for current session
dependencies, renamed two shadowed test variables under `/WX`, and kept manual
audio probes out of ALL_BUILD. Those probes have incomplete upstream link
dependencies and are not automated tests. Optional Rust Spotify-device integration
is not compiled because Cargo is unavailable; the source's external-player path
remains available. MSBuild emits existing MSB8064 asset-directory dependency
warnings; compilation, resources and linking succeed.

No game installation or user saves were changed. No live-game testing was
performed. Before treating the build as gameplay-verified, check first-person
camera/bail/spectator behavior, native controller navigation, terminology HUD
refresh/restoration, rule persistence across restarts, Pop-Shuvit → Shuv and
held 360 → 3 Shuv with Capture Last, combo rows, and multiplayer ownership/matching.
