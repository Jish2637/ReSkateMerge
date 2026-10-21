# MSC Hub Phase 3 test 2: Activities confirmation fix

Client-only test DLL. Keep the Phase 3 test 1 companion server EXE already running successfully. No server configuration, economy, staff implementation or protocol changes are included in this fix.

## Install

Close the game, back up the installed ReSkate.dll, and replace it with client/ReSkate.dll from this folder. Launch the same compatible custom client with automatic updating disabled for this private test. Roll back by closing the game and restoring that backed-up DLL.

## Fixed behavior

Create and Join opened their stake-confirmation popup inside an ImGui card, while the popup was drawn outside that card. The two scopes used different popup IDs. This affected both the floating child-card layout and the full-screen PushID layout, so no confirmation appeared and no game request was sent.

The popup now opens and renders in the same parent scope. Confirmation sends the original structured game action once. If queuing fails, the review stays open; disabled or stale reviews cannot send. Disconnect clears the pending review. Existing server validation, real-wallet wagers, payouts and staff screens are unchanged.

## Verification

The packaged XML records the actual client regression results, including a new test against the real Activities rendering function using ImGui. It covers both card scopes; Coin Flip/Dice create and join; High/Low creation; confirmation-before-send; queue rejection and retry; stale session/round reviews; cancellation; and Start, Leave, Cancel, Higher and Lower action mappings. The same test fails against the original source with: "Create/join failed to open stake confirmation in the parent scope". This expected failure is recorded separately as evidence that the test catches the reported bug.

Previous server regression results remain those of Phase 3 test 1; the server was not modified for this repair. No actual live gameplay is claimed by the automated UI checks.

## Live retest

Open Insert > MSC Hub > Activities. For Coin Flip or Dice, select a valid stake, choose Create lobby and reserve stake, and verify the confirmation appears. Confirm and check that one lobby appears and the wallet reserves one stake. Use a second client to Join, confirm once, and start from the creator. Repeat High/Low creation and direction selection. Check Cancel, Leave/refunds and both Insert menu styles. Staff screens should behave as before.

The package includes SHA-256 checksums and a full client source patch against the recorded Git base. Source changes for this repair are limited to the activity renderer, its regression test, test-target registration and repair documentation/packaging.
