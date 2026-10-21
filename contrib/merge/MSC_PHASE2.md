# MSC Hub Phase 2 test build

This milestone pairs the custom client with the supplied ReSkateServer 2.1.1 MSC Phase1 test2 source. It adds shop/profile/leaderboard/transfer screens; full game lobbies and staff screens remain deferred.

## Source and build

Client changes are in this checkout. The server ZIP is extracted separately to `build/msc-phase2-server-source`; its existing MSC economy implementation is retained. The root checkout's older `Server/` implementation is not replaced.

Configure the companion server with its build directory **inside** that source tree (its existing source-group configuration requires this):

```powershell
cmake -S build/msc-phase2-server-source -B build/msc-phase2-server-source/build-phase2 -G "Visual Studio 18 2026" -A x64 -DDINGOSDK_VERSION=2.1.1 -DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON -DCMAKE_SUPPRESS_REGENERATION=ON
cmake --build build/msc-phase2-server-source/build-phase2 --config Release --target dingosdk_server dingosdk_server_park_tests dingosdk_multiplayer_tests dingosdk_player_stats_tests dingosdk_server_config_tests dingosdk_server_text_tests --parallel 4 -- /nr:false
cmake --build --preset merged-release --target dingosdk_runtime dingosdk_multiplayer_tests --parallel 4 -- /nr:false
```

For Visual Studio PDB-service restrictions, append `/Z7` to the process-local `_CL_` environment variable or use an unrestricted developer shell. Server file-save tests should use a dedicated TEMP/TMP directory beneath this workspace, with filesystem replacement permitted.

The existing numeric versions are retained for these private test binaries. Do not publish them as normal releases. `package_msc_phase2.py` verifies test reports and x64 PE headers, exports both binaries, client/server source patches and an updated server source ZIP, and writes SHA-256 checksums.

## Optional interface contract

The base lobby protocol remains 52. The authenticated client sends the stock chat packet `/msc-ui hello 1`. Only a guest that requested this interface receives packet kind 37 (`msc`); stock clients continue to receive ordinary chat and existing roster traffic. Discovery has its own bounded request budget and cannot generate chat warnings or points activity. Unsupported servers leave the custom client using the first command-based Hub after five seconds.

Kind 37 carries a length-prefixed UTF-8 JSON string, at most 16 KiB. JSON parsing also bounds depth and event count. `v: 1` is mandatory. Counters are unsigned integers; Steam IDs and player-session epochs are decimal strings. Responses are accepted only from the admitted dedicated server, on the current connection, with the current player epoch and a newer revision. Invalid extension messages are ignored without disrupting ordinary gameplay.

Requests include `v`, `epoch`, a monotonically increasing nonzero `request`, and `op`:

- `snapshot`: refresh account/catalogue state and renew the 30-second subscription.
- `collection`: `page`, with ten tags per page.
- `leaderboard`: `kind` (`weekly`, `points`, `time`, `events`) and `page`, ten public rows per page.
- `item`: `kind` (`tag`, `colour`, `joinmsg`, `colourbundle`, `shimmerbundle`) and `value`. This uses the existing handler to quote a purchase or immediately apply a free owned-item change. Join messages use the real `joinmsg set` operation.
- `confirm`: the server-issued `token` of the currently displayed purchase quote.
- `joinmsg-toggle`: `enabled` boolean.
- `pay`: `recipient` Steam ID and positive whole `amount`.
- `close`: stop the subscription.

Responses contain the own-account profile, catalogue/actions/prices, requested collection and public leaderboard page, current quote or null, `request`, monotonic `revision`, and a user-facing `message`. Machine messages never enter chat. The UI does not parse human-readable replies or optimistically change balances/ownership.

Server handlers retain all role/filter checks, configured prices, permanent ownership, free bundle/owned equips, two-minute quote expiry, price revalidation, audit records, synchronous persistence and save-failure rollback. The structured wrapper calls those same functions used by chat. A quote token is specific to the current guest and quote contents/expiry/session. Replacing a quote through chat invalidates the old token. Requests are deduplicated for the session, with 32 cached result messages and a high-water mark preventing evicted requests from executing again.

While subscribed, the server coalesces snapshots to at most once per second and only sends changed state. Display time counters are quantized to 15 seconds. The open Hub reconciles every 15 seconds; closing it sends `close` when possible, otherwise the lease expires. Read and mutation budgets are separate from ordinary chat. A client timeout disables transactions until fresh state arrives. Mutations are never automatically retried or replayed after reconnecting.

## Test installation and rollback

1. Close game, launcher and server. Back up the current client DLL and server executable. Back up the server configuration and entire `data` directory before testing transactions.
2. Replace only `ReSkate.dll` in the compatible custom client and `ReSkateServer.exe` in the existing server folder. Retain scripts, Steam libraries, config, maps, mods and player data.
3. Launch both with `--no-update` for the test, keeping other normal server arguments. This prevents their normal update checks from replacing these local test binaries.
4. Join the enhanced server and open Insert > MSC HUB. Both menu styles share the new interface. Joining an older server retains the command-based Hub.
5. To roll back, stop both and restore the backed-up binaries. No player-data migration is introduced. Restore a data backup only if intentionally undoing test transactions while the server is stopped.

## Live acceptance checklist

Automated tests are not an in-game playtest. Check both menu styles, scrolling, controller focus, player selection and actual preview appearance. Use test balances and small transfers. Confirm real prices and disabled settings, purchase/owned/equipped states, bundle discounts, tag restrictions, join-message edits/toggles and daily/VIP progress. Change an item through chat and confirm the UI updates. Test a replaced/expired purchase quote, insufficient funds, rapid clicks, disconnect during an action and reconnect to another server. A recipient client should see a received transfer after refresh. Verify an ordinary client can still join and use chat commands. Full graphical wagering and staff tools are not part of this milestone.
