# USB PS5 marker input and Steam HID startup recursion — merged.3

## Startup follow-up in merged.3

The merged.2 crash dump reported `0xc00000fd` (stack overflow). Matching PDB
symbols resolved repeated stack addresses to `describe_hid+0x8f` and
`captured_hid_attributes+0xd9`, interleaved with Steam's
`gameoverlayrenderer64.dll+0xc4e9b`. Steam's descriptor wrapper calls the
attributes hook again. The previous bypass prevented ReadFile capture but did
not prevent this attributes observer from starting another descriptor query.

Metadata observation now uses the existing scoped input bypass as a re-entry
gate. Reentrant attributes calls still execute the original API and retain its
result; they skip metadata discovery. The guard is exception-safe and also
leaves the independent overlay reader outside gameplay capture. The new
`hid_metadata_reentrancy` regression exercises that exact callback cycle and
checks one metadata query, two original calls and restoration after errors.

This fixes a startup regression introduced by merged.2. The per-report USB batch
fix below remains in place. Live startup and controller retesting are still
required; automated byte tests alone did not cover the Steam callback cycle.

## Original batch diagnosis and fix

The affected installation's log identified Sony product `0df2` (DualSense Edge)
with USB input report length 64. At the Insert-menu input-ownership transition,
capture instead logged `DualSense over Bluetooth, report 0x01 of 192 bytes, the
game's buffer 2048`. The installed DLL hash matched merged.1.

The capture path inferred USB/Bluetooth from the entire completed transfer.
192 bytes can contain three 64-byte USB reports. Treating those bytes as one
Bluetooth simple report changed the USB D-pad byte to 0 (Up; neutral is 8),
introduced a trigger value, and left later reports unfiltered. A source-level
reproduction confirmed these incorrect writes. This explains a manufactured
marker input; no Bluetooth connection command was identified.

The earlier Codex investigation covered a similar buffer-capacity bug, already
fixed for single reports. It did not establish this batched-transfer case.

The fix caches the HID descriptor's InputReportByteLength with each handle and
carries it through synchronous and asynchronous completion records. One read is
split at that stride; menu/chat capture, gameplay filtering and native first-person
stick filtering process individual reports. Unknown/partial/malformed batches
are not rewritten. The completed-byte boundary remains separate from capacity.

Descriptor queries run outside hid_mutex and inside OverlayInputAccess, avoiding
reentrant ReadFile capture and the historical query-under-lock deadlock. Normal
completed reads use cached metadata. New logs report the actual batch count and
stride, so a 192-byte USB completion should say three reports of 64 bytes (USB).

Regression coverage includes USB DualSense Edge identification; 1, 2, 3, 8 and
32 USB reports; a 2048-byte capacity; all input fields neutral; sequence bytes
preserved; spare capacity untouched; repeated completion; DS4 USB/full Bluetooth;
DualSense full/simple Bluetooth and checksum refresh; rejected unknown, partial
and malformed batches without partial writes.

The automated suite validates byte handling. A live retest must still confirm
opening Insert and chat no longer moves the skater, menu navigation works, and
normal marker controls and first-person stick behavior work after closing them.
