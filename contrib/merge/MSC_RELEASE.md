# ReSkateMerge v0.1.3 / MSC server 2.1.2

This is the standard ReSkateMerge client and server source. The MSC server is built from the tracked Server directory, including shop/profile, ownership, leaderboards, transfers, existing game interfaces, staff tools and server-wide settings. Protocol 52 and optional /msc-ui hello 1 negotiation are preserved. Roulette, Slots and Blackjack are not included.

## Install and update

Client: extract the Windows client ZIP into your existing launcher directory after closing the game, or let the existing ReSkateMerge launcher update normally. Preserve your settings and presets.

Server: stop the server and back up its executable, configuration and entire data directory before installing. Extract the server ZIP into the existing server folder. It contains no replacement live configuration, roles, wallet data or save files. The server retains existing economy settings and reward rules; wagering stays disabled if your configuration disables it.

After installing this 2.1.2 server, its existing update mechanism checks Jish2637/ReSkateMerge. Each published release provides a server archive and a server entry in launcher.json, including the archive hash and executable hash. Source pushes alone do not distribute updates. Both launcher and server honor their existing update settings and --no-update. Updates do not target the upstream repository.

Rollback: stop both processes and restore backed-up binaries. Restore server data with its corresponding backup only if required; restoring it reverses transactions since the backup. Previous GitHub releases remain available. No running server or game installation is changed by publication itself.

## Features and compatibility

Includes all prior ReSkateMerge first-person, Sketchy Landings, terminology and S.K.A.T.E. features plus the MSC Hub. The Hub supports shop/profile/collection, leaders, transfers, Coin Flip, Dice Duel, High/Low and role-filtered staff forms. Graphical dice/coin/card suspense reveals server-issued results; settlement already occurs on the server. Results show win/loss/refund and signed net change. Public lobbies announce once after successful persistence.

Shimmer cyan is the existing formerly green-labelled blue/cyan gradient, distinct from the white/blue shimmer. Ownership keys remain compatible and there are still five shimmers. The new shimmer-cyan command spelling selects the same owned product.

Ordinary clients continue to use existing chat and protocol traffic. Unsupported servers use the command-based Hub fallback. MSC data, quotes and pending reviews are bound to the connection; mutations are deduplicated and never replayed across reconnects. Staff retains original permissions/rank checks. Existing shop, stats and wager persistence formats are retained; optional wager presentation metadata does not decide outcomes.

## Reproduce

Use Visual Studio 2026 x64. Configure with `cmake --preset vs2026-x64` and build `cmake --build --preset merged-release`. For the independently versioned server, configure `cmake --preset vs2026-server-release` and build `cmake --build --preset msc-server-release`. Both builds enable the ReSkateMerge update source. Release packaging requires a clean committed tree, passed full client/server regressions and matching final commit metadata in both builds. Run `python contrib/merge/package_client.py`, then use the existing publisher prepare/publish steps after merging the release source.

Published test metadata records automated results separately from gameplay acceptance. The user has tested earlier MSC builds; the latest presentation/controller appearance and full two-client acceptance still require live verification. No game-build pins or user data are changed by this release.
