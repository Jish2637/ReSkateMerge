# Terminology native test 3

First detector-backed body-spin integration. It measures local body heading and
reads the native natural-footing setting, using the supplied tricklog.12 source
as the implementation reference. Ticker BoardDirection is no longer treated as
skater direction. It is a full ReSkate 2.1.0 runtime, protocol 52, without First
Person Native additions. No installation has been performed automatically.

Use this ReSkate.dll in place of test 2. The included launcher is the verified
unchanged official 2.1.0 EXE; Install ReSkate updates must remain OFF. No additional
scripts are needed. Enable INS > Sandbox > Map & HUD > HUD > Skate trick terminology.

Requested mappings:
- BS 180 Hardflip -> Ghetto-Bird
- Nollie FS 180 Hardflip -> Nollie Ghetto-Bird
- Fakie FS 180 Hardflip -> Fakie Ghetto-Bird
- Switch BS 180 Hardflip -> Switch Ghetto-Bird

Start with the regular BS 180 Hardflip. Leave a few seconds between attempts so
the local attempt-to-ticker association is unambiguous. Test each stance above,
then the opposite-direction 180 in each stance, a plain Hardflip and a 360-body
rotation. Opposite directions/plain/360 should remain native. Held, late, dark,
underflip and unknown/conflicting stance variants are not included in this rule.

If a replacement appears, check its stance is not printed twice; disable while
the row is visible and check the full original name/stance returns. Check restart
persistence and native controller navigation. Preserve ReSkate.log after a short
test session. terminology_spin reports signed yaw, footing availability, local
entity, validity and grounded outcome; terminology_observation records the row's
candidate attempt/direction; terminology_publication records writes/readbacks.

If the status says Waiting for native stance, footing has not been observed and
the writer will leave direction-dependent names unchanged. Do not treat a nearby
Revert as the original trick's direction. Local spin correlation is temporal and
still requires live validation, especially multiplayer/spectating ownership.

Offline Release build and CTest passed. The tests cover all four mappings with
both natural footings, reversed-direction rejection, special modifiers, angle
wraparound, missing samples, landing-phase accumulation, bails, entity replacement,
ambiguous row timing and native text restoration. They do not prove live axes,
stance availability, row association or visual HUD refresh. This is not yet a
confirmed working UI replacement. Full terminology expansion remains deferred.

# Detector source reuse — test 3

Reference: ../!JishFPMFork/ManualCatch/Research/ReferenceSources/Jish-TrickLogger-2.0.7-tricklog.12/ReSkate-TrickLogger-2.0.7-tricklog.12-Source.

| Existing detector code | Native Terminology integration |
|---|---|
| Extension/Skater/trick_logger.cpp: NoBailSkater validation, rig+0x4720 matrix, finite/axis checks and atan2 forward yaw | sample_spin in trick_terminology.cpp; uses current native no_bail_skater and physics watcher after Trainer refresh |
| Extension/Skater/trick_set.h: remainder(yaw-prev,360), net signed spin, native footing and switch reversal | terminology_spin.h; narrow UI-attempt adapter, with additional sample-gap/jump rejection |
| trick_set.h: state 101 landing rotation continues after airborne | UI-attempt adapter continues through state 101 until ordinary ground |
| Extension/Settings/local_gameplay_settings.cpp effective_getter -> trick_logger::gameplay_bool(DefaultStanceIsGoofy) | Same existing getter observes its effective result; atomic natural footing in terminology module; no added detour |
| trick_names.cpp: structured ticker row labels and localized text | Existing adapted reader, including omitted metadata diagnostics from test 2 |
| trick_set.h starting_stance | stance_of normalization; explicit conflicts remain unknown instead of selecting a precedence silently |

New integration responsibilities: native HUD text publication/restoration, user-specified
four-stance recipe, narrow candidate attempt-to-row association, and INS control/persistence.
This does not import the entire old protocol-50 fork, practice matching, general VM
instrumentation, or its scoring-completion classifier. Grounded observations here
are not a decoded native scoring-success verdict. Parent-stack/temporal association
does not prove native scoring ownership. Keep reference GPL-3.0/source attribution.

The .12 reference itself notes the body-axis and footing observations need live
validation. Reusing them does not establish the live sign convention in this mod.
