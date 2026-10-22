# ReSkateMerge v0.1.1 validation

Built on Windows x64 with Visual Studio 2026, MSVC 19.51.36247, Windows SDK
10.0.26100.0 and CMake 4.3.1. Release build with the `vs2026-x64` /
`merged-release` presets succeeded. Launcher binary updates are compiled on
exclusively for Jish2637/ReSkateMerge; the multiplayer protocol remains 52.

CTest: **107 passed, 0 failed, 1 skipped**, out of 108 selected tests. The
installed-game-only `trainer_tuning_names` check skips because
`DINGOSDK_TEST_GAME_ROOT` is unset. Set that CMake cache path to a supported game
installation to run it; invalid explicitly configured game data remains a failure.
Other tests that optionally read game assets exercised their synthetic fixtures.

Coverage includes:

- PR #1 Sketchy Landings: defaults off, threshold/chance sanitization, OR/AND
  evaluation and probability ordering, presets and settings serialization,
  measured airtime/drop freshness, missing measurements and numeric edit
  retention. The release preset explicitly enables the new `steezy_landings`
  suite, which packaging requires. Normal builds keep terminology diagnostics
  at the default zero.
- Launcher updater: stable/preview selection, ignored drafts, resolved assets,
  update preferences and --no-update, wrong hashes, failed downloads, upstream
  binary rejection, mismatched tags and missing manifests. A copied test process
  exercises replacement of its own running EXE, restart-loop suppression and
  cleanup of the previous launcher, entirely inside a disposable fixture.
- First Person Mod 1.36.2 native head movement: default OFF for old settings,
  live stick-settings commands, JSON save/reload and personal preset round trips;
  camera-only passthrough, immediate restoration of capture when disabled,
  camera-and-character exclusion and compatibility mode's camera-only behavior.
- Native glance/toggle emote protection: selective first-tap suppression,
  deliberate 250 ms glance double taps, shoulder-first release, entry/exit,
  producer stalls, separate controller sources, stale glance expiry and
  world/device/menu/focus resets. Sony DS4/DualSense USB/Bluetooth edits retain
  shoulders, face buttons, axes and CRCs, including three-report USB completions.
  DirectInput state and buffered reads preserve unrelated fields; repeated peek
  events do not advance policy, world changes discard old events and buffered
  tap timing uses event timestamps. Runtime refreshes toggle eligibility in
  third person independently of opening the menu.
- S.K.A.T.E. lifecycle regressions with flips, grabs, grinds and ground tricks
  disabled across completion, quit, host closure, disconnect, world/session
  changes and leaving the playable world. Repeated teardown, delayed start/turn
  events and orphaned detection cannot reactivate restrictions. A fresh match
  uses its own new rules. Inactive native callbacks preserve original outputs,
  return values and Windows last-error values without interpreting labels.
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
the pinned ReSkateMerge update source. `BUILD_INFO.json` records the final source commit and
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
refresh/restoration, rule persistence across restarts, Pop-Shuvit â†’ Shuv and
held 360 â†’ 3 Shuv with Capture Last, combo rows, and multiplayer ownership/matching.
For merged.4, finish or cancel a match with grinds disabled and immediately grind
in free skate without restarting; repeat after quitting or disconnecting.
For merged.5, enable Native head movement in camera-only on-foot look and confirm
the changed feel and visible head movement with another player. Verify OFF,
camera-and-character mode and on-board look retain their prior behavior. Check
saved settings and personal presets across restart, single-press glances versus
double-tap emotes, and entering/exiting first person with shoulders released
before D-pad. Repeat Insert/chat capture with the USB PS5 controller. These live
checks remain pending; automated checks do not establish game/observer behavior.

Merged.6 release validation also checks anonymous public release discovery,
manifest/binary downloads and an isolated older-client fixture updated to the
published binary hashes, followed by a second up-to-date check. These results are
recorded separately in versioned `artifacts/public-update-validation-v*.log` files. No game launch
or installation is part of that check. The repository history was scanned with
Gitleaks 8.30.1: all 11 candidates were reviewed as test placeholders, vendored
code/hashes or the documented public Sentry ingestion key, not credentials.

For v0.1.1, live acceptance remains pending: enable Sketchy Landings and vary its
conditions/chances; verify only the local player's selected landings change,
turning it off restores native behavior, world transitions clear active state,
and settings/presets persist across restart. Check terminology renaming/Capture
Last still work without routine diagnostic captures. Automated model tests do
not establish native hook behavior or in-game animation quality.

## Version 0.1.2 validation

The isolated candidate based on `add8ea8` passed five focused suites:
`skate_rules`, `throwdown_relay`, `multiplayer_protocol`, `trick_logger` and
`trick_terminology`. Coverage includes default-equivalent thresholds; slider
endpoints; every native/preference category-mask intersection; malformed,
truncated and unknown wire revisions; frozen host and guest rules; legacy and
mismatched joins; guest judging using host rounding; detector resets; future
matches; recreated preference stores; and Reset persistence.

Release builds are rebuilt from the final committed source with version 0.1.2.
The final full CTest report is saved to `build/vs2026-x64/test-results.xml`;
`BUILD_INFO.json` in the client ZIP records the exact commit, binary checksums,
test count and skips. The packager requires all feature-critical suites to pass
without skips and validates the updater repository, game pins and PE headers.
Client and source ZIPs, manifest and loose binaries are covered by SHA256SUMS.

No game installation or saves are modified by release preparation. Live input
with mouse/keyboard/controller in both INS menu styles, actual game restart,
and a two-client S.K.A.T.E. session remain untested. Check that personal edits
during a match affect only a subsequent throwdown, both peers judge with the
same thresholds, native category restrictions still apply, and Jam/Spot Battle
remain unchanged. These checks and the earlier first-person/Sketchy Landings
live acceptance checks remain pending; this release is not gameplay-verified.

## Version 0.1.3 / server 2.1.2

Release validation runs the full enabled suite against the integrated source, including first-person, controller, terminology, S.K.A.T.E., updater, MSC protocol/session, economy, games, staff and persistence tests. The real ImGui Activities harness covers both card scopes, action mappings, queue failure, stale reviews, sequential dice reveals and signed win/loss/refund presentation. Server host tests cover exactly-once announcement and unchanged shimmer ownership under the cyan alias. Packaging verifies server archive extraction, executable hash and preservation of fixture configuration/wallet files. Final counts and skipped installed-game-only checks are recorded in BUILD_INFO.json and the release notes. Latest in-game visual/controller and two-client checks are not represented as automated successes.

## Version 0.1.4 / server 2.1.3

Release validation reruns the full configured suite from the final merged commit. Added coverage includes compiled-version MSC launcher identity, sequenced FP heartbeats through the server duplicate filter and observer visibility beyond expiry, all-staff leaderboard filtering and no-comply display boundaries. Final counts and environment-dependent skips are recorded in the release BUILD_INFO and test report. Earlier private test 2 passed 41 selected suites; those results are not substituted for this release run. Final live FP, physical rotation and UI/controller acceptance remain unconfirmed. The rejected Unicode patch is excluded.

## Version 0.1.5 client-only SKATE compatibility

The user reported successful in-game testing of the compatibility update before authorizing publication. Automated dual-format codec, per-lobby selection, native replay and enhanced-detector regression results are independent of that user report. Vanilla packets were checked byte-for-byte against the compiled unmodified codec from the supplied vanilla source archive. Vanilla and MSC retain their existing setup paths; no live settings synchronization was added.

The release runs the full configured CTest suite and records final counts in BUILD_INFO and release notes. No server binary is built, replaced or published. The previous server manifest entry remains pinned to its existing version and hashes at its original release URL. Gameplay does not require a server update; older server-side statistics decoders are not changed by this client release.

## Version 0.1.6 MSC startup race

Regression tests reproduce native entry before the authenticated host start, early local/opponent turns, same-tick ordering, duplicate entry/start messages, enhanced submission/replay, cleanup and unsafe recovery. Vanilla startup and bit-exact native attempts retain their existing behavior with no detector or settings changes. The released v0.1.5 relay is tested separately against the new regression to establish that it detects the reported failure. Full configured release results are recorded in BUILD_INFO and release notes; use a fresh temporary root because the existing server fixtures retain fixed-path history across runs.

The new fix requires a live MSC/MSC game to confirm the log-reproduced issue is resolved in-game. No server update is built or published.
