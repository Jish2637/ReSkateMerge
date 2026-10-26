# MSC server 2.1.7 — MSC Hub 0.1.13 and S.K.A.T.E. payouts

This server update includes the supplied MSC Hub 0.1.13 server changes and the MSC_Skate 0.3.6 winner payout bridge. Winners receive 100 MSC per opponent from the roster frozen at match start (100–900 MSC for 2–10 players). Duplicate submissions and restart replays do not pay twice; credits and receipts are persisted together. Live multiplayer payouts were tested successfully by the user.

Install MSC_Skate 0.3.6 on the server authority and participating clients. Ordinary official ReSkate clients can use the MSC plugins; the MSC custom client remains 0.1.11. Native wallet access is restricted to the trusted server plugin MSC_Skate. Server authority cannot independently prove the player's physical trick occurred.

## Updating
Back up the executable, configuration and entire data directory before updating. This executable-only server update preserves your existing configuration, Mods, Plugins, Steam libraries, world-layers.json and wallet data. Automatic updates remain enabled from Jish2637/ReSkateMerge. The release tag stays v0.1.11 to preserve installed client launcher compatibility; only the server manifest entry advances to 2.1.7.

## Wallet migration and rollback
Wallet storage advances to schema 4. Older server executables cannot read schema 4. Rollback requires stopping the server and restoring both the older executable and the matching pre-upgrade data backup. Restoring that backup reverses transactions made since it was taken.

## Source and validation
The server source is committed on main and included in the versioned source ZIP. BUILD_INFO.json identifies the exact source commit, executable hash and test results. The original client source tag and client downloads remain unchanged. This update carries forward the supplied Hub server improvements, including roulette/blackjack and wager handling changes.
