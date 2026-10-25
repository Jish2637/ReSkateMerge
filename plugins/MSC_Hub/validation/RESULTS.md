# Validation status

Required automated validation: **6/6 CTest suites passed** (details in the XML and logs alongside this file):

- Official unmodified plugin sandbox regression suite (`plugins`).
- MSC Hub running in that sandbox (`msc_hub_plugin`): all screens, four shop categories, four activities and 134 staff forms; non-staff transfer drafts across snapshots/roster reordering; visible Send/confirm flow; repeated direct bets, cooldown/insufficient/disabled/invalid-bet checks; optional details; disconnect clearing; adversarial transport checks.
- Protocol-55 packet compatibility (`msc_protocol55`).
- Player-stats/economy persistence, including all 125 Slots outcomes (`msc_player_stats`).
- Existing server configuration tests (`server_config`).
- Existing MSC shop/interface and activities/staff host regressions plus the new plugin bridge integration tests (`msc_hub_server`).

Broader non-required check: `server_park_tests` fails with **“A poll's question or result was repeated in chat”**. This result is preserved in `upstream-server-parks.log`; the broader suite is not claimed to pass. Its cause has not been established by a pristine-baseline comparison.

The server compiles as Windows x64 version 2.1.5, with auto-update disabled. Builds emit CMake/MSBuild warnings about missing upstream `assets`/`assets/emotes` dependencies; these do not prevent these targets from building. No compiler warnings in the changed C++ code were suppressed.

**Live feedback:** the user reports 0.1.0 works in-game, with a missing transfer action, cluttered shop and tedious per-bet confirmations. Preview 0.1.1 addresses those issues. The changed layouts and interactions have not been live-tested by the agent; targeted two-client/controller acceptance remains pending in `docs/ACCEPTANCE.md`. API support was tested against the pinned official source; this does not certify every future release, Linux build or older installed MSC binary.

This update changes only the client plugin. Server protocol, executable and economy files are unchanged; the existing 0.1.0 bridge server remains compatible.
