# MSC Hub Phase 3 private test build

This pair adds interfaces for the existing Coin Flip, Dice Duel and High/Low games, plus permission-filtered staff forms and server-wide settings. Phase 2 shop/profile/collection/leaderboard/transfer features remain available. Roulette, Slots and Blackjack are future work and are not included.

## Install and rollback

1. Close the game and stop the server. Back up the currently installed client DLL and server EXE, and the entire server configuration/data directory, including player-stats.json and roles.json.
2. Replace the client ReSkate.dll with client/ReSkate.dll and the companion server executable with server/ReSkateServer.exe. Keep your existing configuration and data alongside the server. Launch the compatible custom client and server with automatic updating disabled (`--no-update`) for this private test.
3. Connect, open Insert > MSC Hub > Activities for games. Staff appears only for a role with existing staff privileges. Both Insert menu styles use these panels. Staff role assignments and permissions remain controlled by your server.
4. To roll back, close both processes, restore the backed-up binaries and configuration/data together, and restart. Restoring old data also reverses transactions made after that backup; stop testing before restoring it.

No deployment, launcher publication, version bump or automatic economy-setting change is included. The server's existing wagers-enabled setting must be on for games to accept stakes; it stays off when your configuration says off. Administrator unlimited shop credit does not supply wager funds: games use the real wallet.

## Games

The server supplies stake limits, availability, current lobby, creator, members, reserved stakes, remaining refund time and authoritative results. Creating or joining a game shows a stake confirmation. Lobby creators can start or cancel; members can leave for the existing refund behavior. High/Low shows the first card, legal directions and server-issued second card/result. Dice results show the server's recorded faces, with at most 24 displayed rolls. For large lobbies the roster shows up to ten members, prioritizing you, the creator and winner; counts and pot always include everyone. Existing wager RNG, payout rules, persistence and audit behavior remain authoritative.

Optional presentation metadata is added to wager records using the existing persistence schema. It is never used to decide payouts. Older records without this metadata still load; they show existing result text without newly recorded dice/card details.

## Staff

A fixed registry of forms covers player moderation, voice/text restrictions, tool locks, roles, teleport, messages, votes, saved builds, object cleanup, maps/rotation/park mods, server configuration, gameplay permissions and anti-cheat settings. The server lists only actions allowed by the caller's existing command permissions. A moderator does not gain server-wide permissions merely by receiving this DLL.

Queries return server text in the staff result panel. Every mutating form first obtains a server-issued review valid for 60 seconds; confirmation executes through the original command dispatcher as the actual caller. Rank checks, persistence, audits and original command validation still apply. Changing the review, caller permissions, selected-player session or underlying build/object preview invalidates the relevant confirmation. Password values are hidden in review summaries. Existing build overwrite/unload/delete confirmations remain a separate explicit action.

Console-only shutdown, self-update and arbitrary raw-command execution are not offered. Private player account data is not added to staff roster snapshots.

## Compatibility and refresh

The base protocol remains 52. The MSC JSON extension remains opt-in (`/msc-ui hello 1`); ordinary clients receive no machine packets. Phase 3 capability fields are optional, so the client retains Phase 2 screens on a Phase 2 server and command-based fallback on an older unsupported server. No new games are simulated locally. Snapshots use current connection epochs, revisions and bounded payloads. Duplicate mutation request IDs do not execute twice; requests are never replayed automatically after reconnect.

Games and staff are separate on-demand snapshot sections to keep the existing 16 KiB payload cap. Changed subscribed state is coalesced and sent at most once per second, with manual refresh and 15-second reconciliation while open. Disconnect clears server data and pending reviews.

## Verification and live acceptance

The packaged XML files record actual automated regression results. Persistent checks cover protocol bounds/negotiation/fallback, game catalogue and real-host wager actions, stale round rejection, refunds, duplicate mutation handling, High/Low privacy and outcomes, Dice faces, staff capability filtering, preview-without-change, confirmation replacement/expiry, invalid fields, permission revocation and selected-player reconnect. Existing server wager/shop/stats/build/config regressions and client multiplayer/menu/chat checks also run.

In-game appearance and controller usability still require live testing. Use two clients to test each game: create/join/start, cancellation/leave refunds, timeouts, disabled games, insufficient funds and repeated clicks. Check balances before/after against server results. With player/moderator/admin roles, verify available forms, equal/higher rank restrictions, messages/votes, teleport, object previews and saved builds, then review/confirm a reversible server setting. Reconnect a selected player during review and verify refusal. Confirm ordinary-client connectivity, Phase 2-server behavior and legacy fallback. Record live results separately from the automated reports.

## Reproduce

Client sources are this checkout; the supplied server ZIP is maintained separately at build/msc-phase2-server-source. The root checkout's older Server directory is untouched. The package includes the complete modified server source ZIP, source patches, source base identities and SHA-256 checksums.

Use Visual Studio 2026 x64 Release. From this checkout:

```powershell
cmake --build --preset merged-release --target dingosdk_runtime dingosdk_multiplayer_tests --parallel 4 -- /nr:false
cmake -S build/msc-phase2-server-source -B build/msc-phase2-server-source/build-phase2 -G "Visual Studio 18 2026" -A x64 -DDINGOSDK_VERSION=2.1.1 -DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON -DCMAKE_SUPPRESS_REGENERATION=ON
cmake --build build/msc-phase2-server-source/build-phase2 --config Release --target dingosdk_server dingosdk_server_park_tests dingosdk_multiplayer_tests dingosdk_player_stats_tests dingosdk_server_config_tests dingosdk_server_text_tests --parallel 4 -- /nr:false
```

If PDB-service restrictions apply, set process-local `_CL_=/Z7`. Server save/rollback tests require filesystem replacement permission and a dedicated TEMP/TMP directory. Build directories must stay inside their respective source trees. Run the same CTest selections recorded in verification, writing msc-phase3-tests.xml in each build directory, then run `python contrib/merge/package_msc_phase3.py`. Source exports contain code/configuration templates, not live server data.
