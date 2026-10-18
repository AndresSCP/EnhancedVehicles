# Vehicle lifecycle audit — 17 October 2026

Scope: installed v58 fleet controller with v59 native bridge. Source inspection only; no measured RAM/VRAM profile or long-playthrough endurance test.

Deploy uses PlaceAtMe with force-persist enabled, retains the new reference in ExtraVehicles and enables it. A configured simultaneous vehicle limit is checked before creation. A deployment request at the limit does not create a new vehicle. NextExtraVehicleNumber is a monotonically increasing display-label counter, not a count of live objects.

Recall refuses the active or occupied vehicle. It disables the owned extra, removes it from ExtraVehicles, selects the active vehicle, and calls ObjectReference.Delete. This is a deletion request, not a verified immediate release of every engine resource. No guarantee is made that the game's working set or save size shrinks after a call. Unloaded deployed vehicles are distinct from recalled/deleted vehicles.

Issue found: NamedExtraVehicles retains one reference after each successful naming operation and v58 never removes a retired vehicle from that array. Consequently deployment/recall cycles grow the historical naming array; these retained Papyrus handles may delay final reclamation. Source inspection alone does not quantify retained geometry/physics memory or prove that all recalled instances stay fully resident.

Controller v60 candidate removes historical naming references after recall and rebuilds the naming array at initialization/load from current ExtraVehicles membership, excluding nulls and duplicates. Numbering is preserved and existing vehicles are not renumbered. Papyrus compilation passed. This candidate is staged only, not installed or tested in game. Existing occupied-save recovery is outside this change's scope.

Subsequent status: the naming cleanup is included in the installed/tested controller v63 lineage. Its presence does not establish a measured handle-usage plateau. Native v66 source also expires the retained lead owner after its two-second observation lease and releases replaced owners outside the lock; local fixtures verify outstanding snapshots remain valid and ownership balances. That native change is not yet installed/game-tested at this review.

Native state is bounded in the inspected implementation: eight tracked passenger entries, one driver registry, eight calibration identities (numeric values, not retained vehicle pointers), and one retained lead vehicle replaced when another valid lead is observed. The lead's reference may remain retained after observation expires; this is one slot, not an unbounded deployment history. Trail runtime capture/target selection is disabled in v59. Local reference-balance fixtures do not replace an in-game memory profile.

Do not describe the mod as leak-free or give a memory budget without measurement. Long-session CPU activity, loaded vehicle assets/physics, engine caching, persistent save records and retained script/native references are different costs and require separate observation.

## Pointer-handle validation requirement

RAM usage alone is insufficient. Retaining an existing reference in several variables does not establish a new engine pointer-handle slot per variable; it can instead keep an existing object alive. Repeated creation and failed reclamation are the relevant lifecycle concern. A vehicle's runtime dependencies also prevent assuming exactly one consumed slot per deployment.

Before claiming long-playthrough stability, compare pointer-handle dumps before deployment, after a bounded series of deploy/recall cycles, and after unloading/reloading under comparable conditions. Use a control interval without deployment because normal world activity also changes counts. Inspect specific retired vehicle identities where available rather than demanding identical global totals. The expected result is no unexplained growth proportional to retired deployment history, with a bounded number of currently owned extras.

Starfield Engine Fixes author documentation describes DumpPointerHandles/dph, reference/cell totals and optional plugin-level information. This is a diagnostic option, not a new runtime requirement for Enhanced Vehicles. Source reviewed 17 October 2026: https://www.nexusmods.com/starfield/mods/10457 . Public pointer-pool research also exists at https://github.com/1001Bits/Pointer-Handle-Limit-Fix . No handle-pool expansion or engine memory-manager modification is included in this mod's cleanup candidate.
