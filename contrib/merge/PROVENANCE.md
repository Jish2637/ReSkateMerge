# ReSkateMerge source provenance

Integration order:

1. `Jish2637/ReSkate`, `skate-trick-detection-fixes`: `de58cc72245e72dbbff69118ed49c1cc4ca07d64`.
2. Merge `jish-fpm-native`: `9b1d21a72a34f2b709e86daa3fd508d1ee56e2e3`, retaining both parents and branch histories.
3. Integrate `Terminology-Source-test27-2026-10-20.zip`.

ZIP SHA-256: `dc3b6b2542fe57e21f04d6942da7a0ffa3300ca066f60a0cba83e9e2c592deea`.

The ZIP contains a complete source snapshot without Git metadata. Comparing its
file blobs against the latest 100 reachable commits found the closest source tree
at `ba1245a5af51892e90f2be8637dd69a7e20f5b77`. This is a reconstruction reference,
not a claimed exact ZIP parent. Changes were merged file by file against this
revision, preserving newer detector and FPM changes and files absent from the ZIP.

Included terminology changes cover the native publisher and model unlock boundary,
expression observations, natural footing, trainer options, editor and schema 5
rules, retained native labels, combo presentation, overlays and regression tests.
The independently added trick metadata and address headers already match the
detection branch. Its localization catalog gains the additive ambiguity query.

Excluded snapshot differences: newline-only changes under External and the older
`Launcher/gui_home.cpp`, which would remove newer launcher announcements, legal
dialog routing and mod handling. None implement terminology. Assets and bundled
dependencies remain those from the merged branches.

Integration adaptations:

- One effective gameplay-setting read feeds both the logger and terminology,
  retaining the original result and Windows last-error value.
- The existing expression hooks run both observers around one native invocation.
- Hall of Meat owns the shared UI writer detour; terminology forwards through its
  original relay once. The model-unlock hook is terminology's separate boundary.
- Detector observations recover native labels from terminology's tracked edits,
  including hidden combo components. Synthetic combo rows are excluded from detection.
- Terminology includes the detector's physical tracker rather than defining a
  second copy in the same namespace. A const evidence accessor is added.
- Saved terminology schemas 1–5 remain accepted, with schema 5 emitted unchanged.

GPL-3.0 and original attribution remain in the source tree. Historical branch and
ZIP documents describe their original builds; this file and the root README
describe the combined build. No original upstream repository was modified.

## First-person update in merged.5

Selectively ported the requested features from Jish2637/JishsFirstPersonMod
1.36.2, fetched on 2026-10-20 at
`fe80fa7bf53d69ef0c2a6121802f76ae2c36f068`. The original native integration
reference remains `974e91e05b622c5b4b7ea0b78e09be0f18c8edb3`.

- Imported the native-head-look eligibility policy and saved boolean, adapted to
  the native JSON settings, profile command and existing ImGui menu. Default OFF;
  camera-only/compatibility eligibility matches upstream. No head bone writes,
  new native hooks, camera math changes or multiplayer protocol changes.
- Imported the glance double-tap and toggle hold-latch policy with vendor types
  namespaced. The native action binding is the sole toggle owner. Suppression
  covers entry and exit without a second companion activation path.
- Adapted the policy to this client's existing XInput, per-report Sony HID,
  Raw Input and verified Sony DirectInput state/buffered readers. DirectInput
  object usages, VID/PID and offsets are checked before selective POV edits.
  Private readers bypass filtering, while menu/focus/context invalidations clear
  latches. Toggle latches survive camera transitions and producer stalls.
- Retained merged.3 USB batching/Steam metadata reentrancy protection and merged.4
  S.K.A.T.E. teardown. The standalone loader, address contracts, updater, MSC
  integration and unrelated marker reset changes were not imported.

Upstream SHA-256 references:
`src/native_head_look.h`: `4604b3c9cc807561fbaf0f8a1d25420a62a93f505978fb3722a6ec53bf99f73f`;
`src/glance_emote_policy.h`: `a3479376cfb90daf01058b2dde3bf7f80c6ff7a20ed335e092735f10634f5e35`.
Original GPL licensing and attribution remain intact.

## Public update channel in merged.6

The launcher and runtime come only from Jish2637/ReSkateMerge release assets.
The new schema-1 launcher manifest retains the game and DepotDownloader values
from the original ReSkate v2.1.0 `launcher.json`, retrieved from its public release
and cross-checked against `Engine/Game/Build/supported_build.h`. It does not
import upstream launcher/runtime/server downloads. Existing saved update choices
and --no-update remain effective. The repository is made public with the user's
explicit authorization after source-history review and verified draft assets.
