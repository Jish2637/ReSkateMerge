# MSC Hub

Your Midnight Skate Club profile, points, shop and activities — together in one in-game menu.

## What you can do

- **Earn and track MSC points.** See your wallet, lifetime and weekly points, active playtime, daily reward progress, streak, visits, event participation and progress toward VIP.
- **Make your name your own.** Browse standard colours, animated shimmers, custom tags, join messages and bundles. Preview your choice, check the price and equip what you own.
- **Keep your collection together.** Find purchased tags and shimmers, change your active tag or colour, and turn your join message on or off.
- **Send points to friends.** Choose a connected player, enter an amount and confirm the transfer.
- **Climb the leaderboards.** Compare weekly points, lifetime points, active playtime and event participation.
- **Play MSC activities.** Challenge other players in Coin Flip and Dice Duel, try High/Low, or spin the Slots reels. Set a bet once and reuse it with a click.
- **Manage your server.** Staff get tools for moderation, roles, messages, maps, builds, gameplay settings and more, according to their server permissions.

## Getting started

1. Install **MSC Hub** through your ReSkate mod manager and enable it.
2. Join an MSC server with Hub support.
3. Open **Insert > Plugins > MSC HUB**.

Use the standard **ReSkate 2.1.2 or newer** client with plugin support. You do not need the MSC custom client or launcher. When updating the mod, keep only one MSC Hub version enabled.

## Playing activities

Choose an activity and set your bet. **Spin**, **Create lobby** or **Draw first card** uses that amount immediately. **Join** uses the bet shown by the lobby. Each game remembers its own bet while you stay connected, so you can switch between games without entering it again.

Open **Rules & payouts**, **Round details** or **Result details** for more information. Activities use your MSC wallet balance and follow the server's bet limits, cooldowns and wagering settings. The server records all results and payouts.

## Help

- **No Hub connection?** Check that the mod is enabled and the server supports MSC Hub. Server owners need the Hub-enabled MSC server update.
- **An action is unavailable?** The page shows the reason, such as insufficient points, a cooldown, server settings or permissions.
- **A transfer or purchase did not respond?** Select **Refresh** and check your wallet or collection before trying again.

MSC points, purchases and activity settings belong to the server you join. This plugin adds the Hub menu; it does not include the MSC client's camera or enhanced S.K.A.T.E. features.

## For server owners

Use the Hub-enabled server update from [ReSkateMerge](https://github.com/Jish2637/ReSkateMerge/releases). To offer the mod to joining players, add its Thunderstore package ID to your server configuration:

```json
"plugins": {
  "required": ["MSC_Midnight_Skate_Club-MSC_Hub-0.1.1"]
}
```

This requires version 0.1.1 or newer. Existing server economy and permission settings still apply.

Created for **MSC — Midnight Skate Club**. [Source and issue reports](https://github.com/Jish2637/ReSkateMerge) · GPL-3.0.
