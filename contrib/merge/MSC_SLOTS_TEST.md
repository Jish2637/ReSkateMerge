# MSC Hub Slots - client/server test implementation

This feature ships in client **0.1.7** and server **2.1.4**. Build the Windows ReSkate.dll and dedicated server from the same source snapshot. Back up configuration and all server data before updating.

## Scope

- MSC Hub > Activities includes **Slots**, in addition to the unchanged Coin Flip, Dice Duel, and High / Low games.
- New clients opt in to slots with `slots_ui: true` on existing MSC UI requests. The protocol stays at version 1 and older Hub clients continue to see the original three games. Vanilla clients do not receive the opt-in MSC Hub game UI.
- Slots are **Hub-only**. There is no `/slots` chat command or chat-based spin. The server never broadcasts spins or prizes in public chat.
- The server chooses three uniform symbols with its existing unbiased `draw` RNG; it settles stake and payout in a single synchronous `wager_commit`, including persisted audit ledger and rollback on storage failure.
- The client animates three reels **after** receiving the server's settled result; it never calculates or submits a reward. Its display has the current spin's result, gross payout, stake and net result.
- MSC Hub shows up to five recent **winning** slot spins from the already-persisted wager ledger (name, gross payout and multiplier). Other players cannot read another player's reel sequence or losing spins.
- A server-enforced 3.5-second cooldown prevents rapid repeat spins. The client disables the button while the server reports a cooldown. Existing request-ID deduplication prevents duplicate debits.
- Existing role/permission checks, real spendable MSC wallet, stats-failure pause and the global `wagers.enabled` switch apply. Administrative unlimited shop points do not fund slots.

## Payout rules (initial tuning)

Each of the three reels independently draws one of five equally likely symbols.

| Combination | Gross payout |
|---|---|
| CHERRY / CHERRY / CHERRY | 3 x stake |
| LEMON / LEMON / LEMON | 5 x stake |
| BELL / BELL / BELL | 8 x stake |
| SEVEN / SEVEN / SEVEN | 15 x stake |
| DIAMOND / DIAMOND / DIAMOND | 25 x stake |
| Any other exactly matching pair | 1 x stake (refund) |
| All different | 0 |

Among all 125 equally likely outcomes: 5 triples, 60 pairs and 60 losses. The expected gross payout is **116/125 = 92.8% of the stake**, or a 7.2% points sink. No real-money functionality is introduced.

## Server configuration

Existing `wagers.enabled` must be `true` for any game to take points. These optional keys now exist under the existing `wagers` section of the server JSON configuration:

- `slots_enabled` (default `true`): individually enable/disable Slots.
- `slots_max_stake` (default `500`): maximum points for one spin.
- `min_stake` (existing, default `10`): minimum for slots as for the other games.

If `wagers.enabled` is still false, Slots will show as disabled rather than secretly enabling wagering. No existing settings are overwritten by this source patch.

## Changed areas

Client: `Extension/UI/Overlay/msc_features.cpp`, `Extension/UI/Overlay/msc_hub.cpp`, `Engine/Game/Multiplayer/msc_features.h`, `Engine/Game/Multiplayer/msc_interface.h`.

Server: `Server/slot_rules.h`, `Server/msc_features.cpp`, `Server/msc_expansion.cpp`, `Server/msc_interface.cpp`, `Server/server_host.h`, `Server/server_config.cpp`, `Server/player_stats.h`, `Server/player_stats.cpp`.

Tests: `Server/Test/slot_rules_tests.cpp`, `Server/Test/msc_features_host_cases.inc`, `Server/Test/server_config_tests.cpp`, `Extension/UI/Overlay/Test/msc_activities_ui_tests.cpp`, `CMakeLists.txt`.

## Verification on this Windows checkout

The attached patch's earlier verification claims are not used as evidence for this build. Local automated results are recorded in the test package's TEST-RESULTS.md and CTest XML.

Added checks exercise all 125 settled outcomes through save/reload and restart refund handling, legacy Hub requests without Slots opt-in, refusal of non-opted-in spin requests, reconnect capability reset, actual filesystem write failure with balance/ledger/sequence rollback, blocked retries while the economy is paused, and recovery in a newly constructed server Host after restart. Host tests use simulated clients and the real server dispatcher; they do not prove live interoperability with older client binaries.

## Local testing and rollback

Use a separate client installation and an isolated test server copied from a backup. Slots needs the paired client and server. Enable `wagers.enabled` on that test server; do not change your live server to try this build.

The local build uses a `slots.local` version suffix and disables launcher binary updates so it retains the experimental DLL. That experimental package was not published. Production release builds re-enable the standard launcher update channel.

Before starting, back up the server configuration and entire data directory. Once a Slots spin is saved, the older server 2.1.3 loader rejects its ledger records and pauses the economy. To roll back, stop the test server and restore both the previous executable and its matching pre-test data backup. Restoring data reverses transactions since that backup; never reuse test balances on the live server.

## Live acceptance

Back up your server executable/config and `data/player-stats.json`, build the paired client and server, enable global wagers, then check: Activities presents Slots only on the new client; stakes debit once; all reels stop on the server-issued values; a losing spin decreases the wallet, a pair refunds, a triple pays the displayed multiplier; balance persists across server restart; duplicate action identifiers do not pay twice; rapid spinning is blocked; a second MSC client sees winning spin announcements but not private reel results; vanilla clients still connect normally and receive no Slots UI. Test disconnection after sending a spin, and verify the saved balance before submitting another spin.

Blackjack and Roulette can use the same structured Hub action/snapshot and wager/persistence tools, without any shared mutable slot-game state or chat commands.
