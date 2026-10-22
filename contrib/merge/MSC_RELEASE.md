# ReSkateMerge MSC v0.1.8

Patch for object dropper.

# ReSkateMerge MSC v0.1.7 / MSC server 2.1.4 - Hub Slots

Adds Slots to MSC Hub Activities, with server-generated reels, persisted stakes and payouts, a 3.5-second cooldown and recent winning spins. No public chat spin messages or chat-based Slots command. Global wagering and server permissions still apply. Optional wagers.slots_enabled defaults true; wagers.slots_max_stake defaults 500. Global wagering remains disabled wherever the existing configuration disables it.

Slots requires the updated client and server. Older clients retain their original three activities through explicit per-session Slots opt-in. No S.K.A.T.E. source or behavior is changed. Production launcher binary updates are enabled through the existing release channel.

Back up the executable, configuration and entire server data directory before updating. Once Slots records are saved, server 2.1.3 cannot load that ledger. Rollback requires restoring BOTH the prior server binary and its matching pre-update data backup; restoring data reverses transactions since that backup. Never test using a live economy backup that will later be copied back into production.

Automated checks cover all 125 settled outcomes surviving save/reload without repeat payouts, older Hub catalogue and capability restrictions, reconnect capability reset, failed-save balance/ledger/sequence rollback, blocked retries and recovery in a fresh Host. The local full suite passed 113 tests with one game-data-dependent trainer check skipped. The user reported that the local paired build works and authorized publication. Actual older-client binary sessions are not separately recorded as verified.

# ReSkateMerge MSC v0.1.6 - client-only MSC SKATE startup hotfix

Retain early native match entry for an enhanced-capable lobby until the authoritative MSC start arrives, then attach the relay and detector to the event already running. Early turn information survives; duplicate starts cannot reset it. If the native match has already processed an attempt or its participants/setup differ, the local MSC match exits with a retry notice rather than fabricating an enhanced result.

Vanilla SKATE gameplay, judging, setup, native submissions and turns remain unchanged. No detector calculations, packet formats or server source change. The server remains 2.1.3 at its existing v0.1.4 archive and hashes. Client updates continue through the standard MSC launcher.

The logged guest startup order is covered by automated regressions. Actual two-MSC-client gameplay verification of this hotfix is still pending. Earlier live feedback about v0.1.5 is not claimed as validation of this fix. The packager records live SKATE success only when explicitly passed --user-tested-skate.

# ReSkateMerge MSC v0.1.5 — client-only SKATE compatibility

Automatic per-throwdown compatibility selects native vanilla SKATE whenever a participant is vanilla or unconfirmed; all confirmed MSC participants retain the existing complete enhanced detector. The selection is locked before gameplay. Native flags, lobby and setup flows remain in use. The user reported successful live compatibility testing.

Client installations update normally through the existing launcher. No server update is required for gameplay. This release does not rebuild or distribute the server: launcher.json retains server 2.1.3 and its previous archive/executable hashes, with a fixed URL to its original v0.1.4 asset. Existing 2.1.3 servers therefore see no binary change. Compatibility gameplay and optional server statistics decoding are separate; an older server decoder may not record vanilla-format attempts.

For client-only packaging, use `python contrib/merge/package_client.py --client-only --previous-manifest PATH --previous-tag v0.1.4` after committing, rebuilding the client and completing the configured tests. The existing publisher detects client-only BUILD_INFO and uploads no server assets.

# ReSkateMerge MSC v0.1.4 / MSC server 2.1.3

This is the standard ReSkateMerge client and server source. The MSC server is built from the tracked Server directory, including shop/profile, ownership, leaderboards, transfers, existing game interfaces, staff tools and server-wide settings. Protocol 52 and optional /msc-ui hello 1 negotiation are preserved. Roulette, Slots and Blackjack are not included.

## Install and update

Client: extract the Windows client ZIP into your existing launcher directory after closing the game, or let the existing ReSkateMerge launcher update normally. Preserve your settings and presets.

Server: stop the server and back up its executable, configuration and entire data directory before installing. Extract the server ZIP into the existing server folder. It contains no replacement live configuration, roles, wallet data or save files. The server retains existing economy settings and reward rules; wagering stays disabled if your configuration disables it.

After installing this 2.1.3 server, its existing update mechanism checks Jish2637/ReSkateMerge. Each published release provides a server archive and a server entry in launcher.json, including the archive hash and executable hash. Source pushes alone do not distribute updates. Both launcher and server honor their existing update settings and --no-update. Updates do not target the upstream repository.

Rollback: stop both processes and restore backed-up binaries. Restore server data with its corresponding backup only if required; restoring it reverses transactions since the backup. Previous GitHub releases remain available. No running server or game installation is changed by publication itself.

## Features and compatibility

Includes all prior ReSkateMerge first-person, Sketchy Landings, terminology and S.K.A.T.E. features plus the MSC Hub. The Hub supports shop/profile/collection, leaders, transfers, Coin Flip, Dice Duel, High/Low and role-filtered staff forms. Graphical dice/coin/card suspense reveals server-issued results; settlement already occurs on the server. Results show win/loss/refund and signed net change. Public lobbies announce once after successful persistence.

Shimmer cyan is the existing formerly green-labelled blue/cyan gradient, distinct from the white/blue shimmer. Ownership keys remain compatible and there are still five shimmers. The new shimmer-cyan command spelling selects the same owned product.

Ordinary clients continue to use existing chat and protocol traffic. Unsupported servers use the command-based Hub fallback. MSC data, quotes and pending reviews are bound to the connection; mutations are deduplicated and never replayed across reconnects. Staff retains original permissions/rank checks. Existing shop, stats and wager persistence formats are retained; optional wager presentation metadata does not decide outcomes.

## Reproduce

Use Visual Studio 2026 x64. Configure with `cmake --preset vs2026-x64` and build `cmake --build --preset merged-release`. For the independently versioned server, configure `cmake --preset vs2026-server-release` and build `cmake --build --preset msc-server-release`. Both builds enable the ReSkateMerge update source. Release packaging requires a clean committed tree, passed full client/server regressions and matching final commit metadata in both builds. Run `python contrib/merge/package_client.py`, then use the existing publisher prepare/publish steps after merging the release source.

Published test metadata records automated results separately from gameplay acceptance. The user has tested earlier MSC builds; the latest presentation/controller appearance and full two-client acceptance still require live verification. No game-build pins or user data are changed by this release.

## Version 0.1.4 / server 2.1.3 changes

The launcher shows MSC Launcher v0.1.4 from its compiled version, independent of remote release metadata, offline mode or saved update preferences. Ready runtime status includes ReSkate MSC. Numeric versions and protocol 52 are unchanged in shape.

No-comply terminology corrects the missing half-turn once at the display boundary, preserving physical tracking and S.K.A.T.E. judging. All staff roles, including offline/custom staff, are excluded before leaderboard ranking, pagination and announcements; balances/statistics remain stored. Automatic FP presence follows actual local camera ownership and uses sequenced five-second heartbeats to pass duplicate filtering, with the existing 15-second expiry. The Unicode role-tag proposal is excluded.

Automated results are recorded in the packaged BUILD_INFO and release notes. The latest FP repair has automated observer coverage; final physical-turn calibration, in-game visual/controller checks and live two-client FP acceptance remain unconfirmed.
