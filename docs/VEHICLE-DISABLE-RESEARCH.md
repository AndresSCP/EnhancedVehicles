# Vehicle health, immobilization and repair — research design

Requested 17 October 2026. Goal: at zero effective vehicle health, keep the vehicle visible but make it unable to drive, boost or fire. No explosion or replacement wreck mesh is required. This is future research, not an implemented feature in native v66. No equivalent health/disabled-state machine was found in the reviewed external vehicle module.

## Do not confuse engine visibility with gameplay disablement

ObjectReference.Disable hides/disables the reference. It is not the requested broken-but-visible condition and must not be used as a health-zero shortcut. SetDestroyed/IsDestroyed and destruction-stage events exist in the local Papyrus declarations, but their actual behavior on rover Furniture references is unverified. A mechanical failure state must also cover player input; merely stopping our convoy AI would still let the player drive it.

## First establish what health means for a rover

The local ObjectReference API exposes GetValue/GetValuePercentage, damage/destruction calls and OnDestroyed/OnDestructionStageChanged. These declarations do not prove that Game.GetHealthAV is a valid vehicle-health source or that every hit damages that value. Zero from an unsupported/missing value must never be interpreted as a destroyed vehicle. The visible HUD may reflect a different component/value or damage receiver.

Read-only experiment requirements:

- Record base type, reference identity, destruction state and candidate health/max values before, during and after controlled damage.
- Correlate hit/weapon events with actual value changes, independent of driver health, boost charge and other gauges.
- Check a standard official rover and at least one supported modded vehicle; determine whether data is shared by base form or unique to its placed reference.
- Investigate immunity flags, destruction records and hit registration before adding artificial health. If native vehicle damage cannot be used, an explicitly documented virtual durability layer needs reliable hit attribution; counting any nearby shot is insufficient.

## Proposed state machine

Operational → Disabled → Repairing → Operational.

Disabled should persist per placed vehicle across save/load. It overrides movement, horizontal/vertical boost and mounted trigger for both AI and player, while preserving collision, appearance and access to repair/recall. Entering zero-health once must not repeatedly play exit actions or warnings. Prevent automated convoy restart and reject assignment to an unavailable vehicle.

Occupants should leave through normal animation when grounded/safe. A flying vehicle needs a separate emergency-descent policy; ejecting everyone at altitude or clearing all inputs immediately can be worse than the original damage. That policy depends on validated ground clearance and available lift and must be designed before enabling aerial failures.

Repair needs an explicit user-facing action and chosen cost/rule; no silent health refill. Recalling/deleting an empty disabled vehicle is separate from repairing it. A registry must forget retired references, keep bounded owned state, avoid personal-content dependencies and preserve unrelated mod-added tags.

## Validation gates

Test player-driven and allied vehicles, health already zero at load, unsupported health values, repeated damage while disabled, repair, save/reload, convoy reassignment and deletion cleanup. Verify resource/flight mods remain functional while Operational. No global modification of every Furniture form or destruction record is planned.
