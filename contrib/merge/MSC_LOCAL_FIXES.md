# Local test: no-comply labels, staff leaderboards and automatic FP

Test 2 repairs FP presence after comparison with JishsFirstPersonMod commit c0d2c4788fcbc1f970d4b3dc013a70f46b43bd6f. Each report now includes a changing uint32 sequence, as the original implementation did, so five-second heartbeats pass the server's 15-second duplicate-chat filter. Core camera ownership no longer depends on optional addon readiness or the normal view's enabled flag. Missing capability on an existing browser row now triggers authenticated MSC discovery too. This repair changes the client only; the companion server is identical to test 1. Install the corrected DLL on each player who wants to advertise FP. Observing players do not need the addon.

This is a private test pair, not a published release. The binary version stamps remain client 0.1.3 / server 2.1.2. The source patch is against the recorded public source commit. No GitHub release or launcher manifest is changed.

## Install and rollback

Close the game and stop the server. Back up the installed ReSkate.dll, server executable, configuration and complete server data directory. Replace the DLL and server EXE with this package's client/ReSkate.dll and server/ReSkateServer.exe. Launch the compatible launcher and server with `--no-update` during testing so the published release does not replace these private binaries. Existing economy settings, roles and persistence remain in place.

Rollback: close both processes and restore the backed-up binaries. Restore paired data backups only if necessary; restoring data reverses transactions since that backup.

## What changes

No-comply: only the final native terminology display is corrected for ID_TRICK_NOCOMPLY with valid rotation evidence. Native acknowledged spin labels receive their missing half-turn; a zero/empty label needs evidence of rotation before showing 180. Unrotated attempts and other tricks retain their labels. The original label is preserved to avoid stacking the correction on later UI updates. Custom terminology replacements keep precedence. Physical trackers, native scoring and S.K.A.T.E. matching/signatures are deliberately unchanged, as requested. Therefore this display fix is not a competitive judging fix.

Leaderboards: one eligibility function excludes every role recognized by the existing staff-permission definition, including offline accounts and custom roles. Hub, chat and automatic announcements use the same filtered rows before pagination/ranking. Hub ties now use the same ranks as chat. Non-staff VIPs remain eligible. Balances and stored statistics are not deleted, and removing staff privileges restores eligibility using existing statistics.

FP: the client observes actual local first-person camera ownership rather than the enabled/requested setting. It recognizes the existing jfp1 Steam capability and MSC server eligibility. A connection without a current advertised FP capability performs optional MSC discovery on an eligible server and waits for the authenticated fp_presence capability before sending presence reports. Reports are reliable machine messages handled by the existing /jish-fp endpoint, sent on changes and every five seconds while active. The server keeps its existing 15-second expiry and session reset. Losing local camera ownership, third person, camera overrides/spectating or a stale camera sample disables the report. Unsupported servers receive no reports. Server-side purchased tags, roles, colours and UTF-8-safe name composition remain unchanged; prefixes do not stack.

## Verification and live acceptance

Packaged XML reports record the actual selected terminology, S.K.A.T.E., multiplayer, MSC host/stats, menu and first-person test results. New cases cover missing half-turn labels, preservation/repeated rendering, staff/custom/offline role filtering, role changes, Hub ties and automatic FP report timing/session isolation. Existing host presence cases verify report privacy, expiry, reconnect and name/tag/role composition.

Live test both binaries together. Check unrotated, 180, 360, 540 and 720 no-complys in both directions/stances; confirm the HUD agrees with the physical turn while judging remains unchanged. Check every leaderboard in the Hub and chat with online/offline moderator/admin/custom staff and ordinary/VIP players. With another client observing, toggle first/third person, switch to other cameras, spectate, disconnect/reconnect and verify [FP] appears/disappears once while purchased tags and role badges remain. Live physical-turn calibration and camera behavior cannot be established by synthetic tests alone.

