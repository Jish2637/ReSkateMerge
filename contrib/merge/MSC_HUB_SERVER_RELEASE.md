# MSC server 2.1.6 — MSC Hub plugin support

Players using standard ReSkate can now open the MSC Hub through the [MSC Hub plugin](https://thunderstore.io/c/reskate/p/MSC_Midnight_Skate_Club/MSC_Hub/). The existing MSC custom client Hub continues to work.

The plugin provides the shop, collection, profile, leaderboards, points transfers, Coin Flip, Dice Duel, High/Low, Slots and permission-filtered staff tools. Balances, purchases, wagering outcomes, refunds and staff permissions remain server-authoritative. The bridge adds private, bounded plugin messages to the existing MSC handlers; it does not change economy formats or reward rules.

## Updating a server

This is a Windows server update for an existing MSC server installation. Keep the existing Steam runtime DLLs and world-layers.json beside the executable. The archive contains no replacement configuration, roles, wallets or other live data.

Back up the executable, configuration and entire data directory before updating. The normal server updater can install this version when automatic updates are enabled and the server's update conditions permit it. `--no-update` or `auto_update: false` disables automatic installation. This published build keeps updates enabled from Jish2637/ReSkateMerge.

For a manual upgrade, stop the server and replace ReSkateServer.exe from the 2.1.6 ZIP. Start the server with the existing configuration and data. No custom client update is required for this server patch; the public MSC client stays at 0.1.11.

To offer the plugin to joining players, merge this section into ReSkateServer.json while the server is stopped:

```json
"plugins": {
  "required": ["MSC_Midnight_Skate_Club-MSC_Hub-0.1.1"]
}
```

This is the published Thunderstore ID and requests version 0.1.1 or newer. Server wagering and permissions still apply; this update does not enable wagering where it was disabled.

## Rollback

Stop the server and restore the previous executable. The bridge adds no persistence migration. If a data restore is needed, restore the complete matching backup, understanding that this reverses later transactions. Keep test-economy data separate from production.

## Validation

The package includes actual automated test results and source/build identity in BUILD_INFO.json. Tests cover the bridge, existing MSC shop/activities/staff operations, persistence, protocol 55 and archive installation preserving configuration/data. The broader server park/poll test has a separately recorded poll-chat assertion failure; it is not claimed to pass. User feedback confirms the plugin works in-game; the latest interface revisions and an actual production auto-update remain separate live acceptance checks.

The existing GitHub release tag remains v0.1.11 because installed launchers validate it against the unchanged client version. Only its server entry advances to 2.1.6. The previous server archive and manifest are preserved for rollback; use the 2.1.6 source ZIP for this server's corresponding source.
