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
