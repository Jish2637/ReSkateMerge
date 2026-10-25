# Feature checklist and live acceptance

Implementation coverage is distinct from live verification. The user reports 0.1.0 works in-game; individual checks below and the changed 0.1.1 interfaces remain pending. Use an isolated economy copy, two standard ReSkate clients with the plugin, then repeat key flows with one MSC client.

## Focused 0.1.1 live checks

Repeat these in both menu styles, first with mouse/keyboard and then controller navigation.

1. Sign in as a non-staff player. Open Transfer, choose a player and enter points. Wait at least 20 seconds and change the roster (join/leave a third player). Verify the amount and chosen Steam ID remain, **Send points** stays visible, and confirmation names the correct recipient/amount. Confirm once and compare both wallets. Check empty/invalid amounts, insufficient funds, no other players and transfer permissions; each should explain why it cannot send.
2. Visit all four Shop categories. Switch items and check that only one product detail panel is shown. Inspect price, ownership, preview, disabled/equipped state, text fields and server quote confirmation. Check compact readability at your normal resolution and UI scale.
3. In Slots set a bet, spin, wait for the server/cooldown and spin again with one click. Confirm the bet stays selected and each accepted click settles only once. Repeat create/join flows for Coin Flip and Dice Duel and the first-card/choice flow for High/Low. Join must use the lobby's displayed bet. Verify no extra stake confirmation appears.
4. Switch games and Hub tabs: each game's bet should remain. Disconnect/change servers: bets and transfer drafts must clear. Check insufficient funds, disabled games and repeated clicks while a request is pending.
5. Expand Round details, Result details and Rules & payouts to verify rosters, authoritative die/card/reel data, refunds and payout information remain accessible. Confirm the collapsed layout keeps the main action easy to reach.

| Area | Implemented behavior | Automated evidence | Live check |
| --- | --- | --- | --- |
| Connection | Negotiation, private chunks, stale-state handling, reload/disconnect/map reset | Official sandbox and transport adversarial tests; real-host bridge | Join, reconnect and change map; verify no old data or action replay |
| Shop | Server catalogue, colour/shimmer preview, custom text, bundles, quotes, confirmation, equip | Real-host quote/purchase and failed-save regressions | Buy/equip each product type; compare authoritative prices and balances |
| Collection | Owned tags, hide/equip, pagination, join-message edit/toggle via Shop/Collection | Existing MSC host interface tests; official API rendering | Multi-page tags; owned colours/shimmers; restart persistence |
| My MSC | Wallet, lifetime/weekly points, active/daily progress, streak, visits, events, VIP | Official API rendering; player-stats regression | Compare existing MSC Hub before/after activity and daily reward |
| Leaderboard | Four rankings and server pagination; staff exclusion retained | MSC host interface and player-stats regressions | Tie ranks, pages and staff roles; active-time display |
| Transfer | Player selection, native amount dialog, review, confirm, no duplicate click | Official dialog/transfer flow and real-host deduplication/rollback | Two clients; verify both wallets; disconnect recipient during review |
| Coin Flip | Create/join/reserve/start/leave/cancel, roster/pot, refunds, coin animation/result | MSC real-host activity tests | Two players, cancellation, timeout, insufficient wallet and repeated clicks |
| Dice Duel | Same lobby controls, authoritative die faces/rerolls and graphical reveal | MSC real-host activity tests | Ties/rerolls, roster truncation, result and net payout |
| High/Low | Reserve/draw, private first card, legal directions, cancel/refund and reveal | MSC real-host privacy/outcome tests | Opponent cannot see private cards; ties and edge cards |
| Slots | Reusable bet, one-click spin, cooldown, three reels, multipliers, optional recent wins and private results | All 125 persisted reel outcomes; MSC real-host activity tests; repeated-spin UI regression | Cooldown, disabled wagering, insufficient wallet, retry/restart |
| Staff | 134 generated forms, categories, fields, roster pagination, role filtering, queries/reviews/confirm/cancel and output | Every form builds in official sandbox; real-host expiry/revocation/rank/target-reconnect tests | Player/moderator/admin, settings, builds/object confirmations and secret handling |
| Compatibility | Original MSC packet path retained alongside plugin path | Real-host native MSC regressions, mixed transport check and protocol-55 codec | Actual stock/MSC binary sessions; both menu styles and controllers |

Also verify native dialog cancellation with Escape/B, long and non-ASCII names, resolution/UI scaling, menu navigation while animations play, and disabled/paused economy presentation. Check authoritative balances after every uncertain transaction. Native controller usability and actual game HUD presentation are not established by sandbox widget construction.

No Roulette, Blackjack, custom first-person camera, enhanced S.K.A.T.E. judging or new reward rules are introduced. The plugin's password-entry dialog limitation is described in the README.
