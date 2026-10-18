# Enhanced Vehicles: vehicle systems, native findings and authoring contract

Research updated 17 October 2026. Runtime inspected: Starfield PC 1.16.244.0, SFSE 0.2.21. Current source baseline: controller v63, native v65 and consolidated plugin records from v61. The controller and native sources compile; local native fixtures pass. Prior gameplay observations and current unresolved issues are separated below and in STATUS.md. Native symbols are descriptive aliases, not recovered Bethesda source names. Addresses are executable-relative RVAs, not portable APIs.

## 1. What this research establishes

The investigation combines installed Papyrus declarations, official vehicle records, animation metadata, executable disassembly, scoped native instrumentation, automated fixtures, logs and user observations. It does not establish that every finding is unpublished elsewhere. Public script APIs and original findings are distinguished below.

| Capability | Evidence and current limit |
|---|---|
| Independent additional vehicle | Deployment, recall and player driving visually observed. Deployment copies the active vehicle base; this is not a complete garage/catalogue system. |
| Multiple passenger seats | Distinct human markers, animated boarding and exit observed in a multi-seat configuration. Assets and marker transforms must support the roles. |
| Two standard REV-8 vehicles | Player plus companion in the lead; allied driver plus companion in the extra visually confirmed. |
| NPC driving pose | Confirmed after ordinary managed entry using scoped animation-value evaluation. Loading an already seated driver can still restore the passenger pose. |
| Allied following | Continuous movement, elevation changes and some obstacle recovery confirmed. This is positional control, not general navigation/pathfinding. |
| Gradual player acceleration | Confirmed input filtering; does not prove that the engine ignores vehicle definition settings. |
| Headlights | On/off mirroring confirmed using each vehicle's own effect sequences. Initial persisted light state before a toggle remains unresolved. |
| Forward/vertical boost | Mapped native action paths confirmed. Automatic vertical pulses observed after stalled recovery; no grounded/headroom query is established. |
| Mounted weapon | Explicit trigger test fired forward. Automatic target engagement has not succeeded; independent turret aiming remains unimplemented. Not a required beta feature. |
| Save/load with occupants | Broken in prior tests. v55 added a correction before the engine's seated-interaction short circuit; game confirmation is pending. |

## 2. Package architecture and external dependencies

The consolidated runtime has one plugin, `EnhancedVehicles.esm`. Its only declared master is `Starfield.esm`. All mod-defined settings, controller records, messages, gadget and extra-human-seat keywords are in this plugin. It contains no custom vehicle base, furniture override, model or demonstration vehicle. The standard seat-role lookups still require the official REV-8 content `SFBGS004.esm` at runtime.

The DLL, compiled scripts and any future assets remain separate files. One ESM does not embed executable code, PEX scripts or meshes. SFSE loads the native bridge; Cassiopeia Papyrus Extender is an external dependency for active-vehicle discovery, furniture marker count/keyword/user queries and related utilities. OSF Settings/UI is optional. No third-party dependency runtime is redistributed.

`EnhancedVehicles.esp` is an alternate editor edition with equivalent records and its master flag cleared. It is not an add-on to the ESM. Load one edition at a time. Our own script helper checks which plugin filename is loaded and resolves its local IDs accordingly. Internal script names are retained for source continuity. The files pass structural checks; opening and saving the ESP in Creation Kit remains unverified.

Changing the prototype plugin identity is not a save migration. Existing placed references, quest state, occupied seats and settings must not be assumed to transfer. Preserve old packages/saves and test the consolidated build separately.

## 3. Seat authoring contract for a demonstration REV-8

A vehicle is an elaborate Furniture reference with interaction markers. Geometry alone does not define capacity. Array index, enabled state, role keyword, position, rotation, entry/exit flags and compatible animation data all matter. An arbitrary Blender node label or a Creation Kit marker name alone does not identify a role.

| Marker index | Role | Exact keyword form |
|---:|---|---|
| 0 | Driver | `VehicleDriverSeat`, SFBGS004.esm local `000019` |
| 1 | Front human passenger | `VehicleCompanionSeat`, SFBGS004.esm local `00001D` |
| 2 | Vasco | `VehicleVascoSeat`, SFBGS004.esm local `00001C`; excluded from human allocation |
| 3 | First extra human passenger | `EVExtraHumanSeat1`, EnhancedVehicles.esm local `000822` |
| 4 | Second extra human passenger | `EVExtraHumanSeat2`, EnhancedVehicles.esm local `000823` |

The ESP editor edition has the same local keyword IDs. Reference the actual keyword Form; creating a different keyword with a similar editor ID is not equivalent. Load-order prefixes are not stable IDs. Arbitrarily numbered additional seat roles are not supported by the current backend.

Do not give every rear seat the standard companion keyword. Separate role keywords and scoped marker preparation were required to prevent overlap. Query occupancy on the spawned reference, because two vehicles using the same base have independent occupants.

In the tested rear layout, marker 3 uses right entry/exit and marker 4 uses left entry/exit. A successful animation correction removed the `Other` entry direction while retaining the intended side: the relevant fields changed from `0x14` to `0x04` and `0x18` to `0x08`. These are specific tested entry fields, not whole-marker replacement bytes or universal encodings for unrelated fields. A changed flag does not create a missing animation. Preserve the driver's and front companion's original roles and assets.

For a demonstration vehicle: author the mesh/transforms in Blender, export through the separately validated vehicle asset pipeline, configure the Furniture marker array in Creation Kit, assign these exact roles and test each entry/exit side. This investigation does not validate a Blender exporter or assume that exported geometry automatically creates valid Furniture markers. Replacement rear animations from the other official vehicle content remain future research.

## 4. Boarding and release

The controller reserves a supported free marker and temporarily adds the corresponding role keyword to the selected follower. Native preparation is scoped to that actor, vehicle and marker. Controller cleanup removes its managed order and requests normal exit; other actors and unrelated vehicle data remain outside the scope.

Natural vehicle interaction at `01315260` takes actor/target handles; the validated path uses non-forced entry with zero forced retries. Full preparation at `01A04C30` matters. Immediate placement can produce a seated result without the expected entry animation. Rear preparation filters candidate seats only for the managed interaction rather than changing global occupancy.

Driver holding validates the matching actor, vehicle handle and marker 0 and expires without a fresh controller lease. Ordinary exit is requested through the validated get-up path; physical input is neutralized before release. The animated driver exit correction was visually confirmed after earlier abrupt teleport exits and collisions.

### Saved-seat short circuit

The Papyrus `SnapIntoInteraction` callback at `01FCA990` eventually calls `01799E50(actor, furniture)`. That routine resolves the current furniture and, at `01799EFE–01799F08`, returns true immediately if it already matches the requested target. Neither the later outer wrapper nor fresh-entry preparation runs.

This explains the previous false-positive recovery: Papyrus returned true while the process-local native occupant registry remained empty. Serialized quest orders and native retained references have different lifetimes. v55 intercepts the earlier routine, validates the loaded actor/target/current marker/role, re-registers the existing occupant and logs `NATIVE_LOAD_REBIND_ACK`. Within this matching scope, successful return also requires native registration. The original function is still called once. Local fixtures cover repeated registration, reference balance and preservation of unrelated results.

Actual-occupant scans can reconstruct cleared quest orders from CPE marker-user queries on registered additional vehicles. An occupied-save regression showed that discovery worked but release did not; discovery alone is not proof of native recovery. The latest early-wrapper correction still requires visual confirmation. Same-process load transitions, loaded graph pose and complete save migration are not claimed solved.

## 5. Why a seated driver initially looked like a passenger

Logical marker 0 does not guarantee the driver animation. Official animation metadata separates NPC and Player sets:

| Set | Decimal key | Hex key |
|---|---:|---|
| NPC | 777300086817692085 | `0AC9864F86A619B5` |
| Player | 13247197233673542069 | `B7D77FB286A619B5` |

The NPC table lacks the Player rover idle clips; lateral entry/exit clips are shared. `FurnitureRover_Wrapping.agx` chooses branches using `iIsPlayer`. Setting that graph variable after seating, swapping a provider or replacing a candidate key alone did not solve the visible pose.

The successful preparation path was `019C47F0 → 01AFA090 → 01AFA460 → 01AFAB40 → 01B003D0 → 01AF8F10`. The temporary evaluation context has a provider at `+08`, actor used for value evaluation at `+18` and variable-list input at `+20`. The actor used to evaluate values is distinct from the destination graph.

For the managed pilot's preparation, the bridge temporarily substitutes the loaded player only as the evaluation actor at context `+18`. The NPC remains the occupant and the destination graph remains its graph. The original callback executes once and the temporary field is restored even through native exceptions. This does not change Actor identity, FormID, player globals or control ownership. It is broader than a proven single-property override; minimal ablation remains future work.

Candidate output rows have 32-byte stride, marker at `+0`, count at `+8`, candidate-array pointer at `+18`. Candidate records have 64-byte stride; key `+28` feeds high-process `+4F0`, auxiliary `+20` feeds `+440`. Only exact matching NPC keys in driver-marker results are replaced. Rear candidates and unrelated data are preserved. Prepared-variable elements have observed eight-byte stride, but their complete semantics are not established.

## 6. Applying physical vehicle input

The native bridge locates a live ECS entry by full key `(componentType << 48) | (vehicleFormID << 16)`. Runtime input type is `0x77`; a matching FormID alone is insufficient. The manager path uses `023FF640`, game-systems `+8B0`, manager `+268` and retained owner `+238`. Read scope enters through `02404230`, lookup through `024105D0`; result pool/index fields are `+10/+18`. Sentinel index `FE0`, invalid counts, mismatched keys and component identity are rejected.

Write entry `02404330` takes **two arguments**, page allocator and pool header. Header is pool `-20`, allocator is header `+8`. An earlier wrong ABI crashed. Access retains the owner, enters the proper native read/write scopes and unwinds them on every exit; component pointers do not escape the query.

Validated component identity includes vtable RVA `04C46F98`, entity identity `+0C`, link `+10` and temporary gate `+579`. The bridge temporarily enables that gate only around original input application `01324650`, then restores its previous value.

| Payload offset | Established role |
|---|---|
| `+10` | Steering float |
| `+14` | Forward accelerator float |
| `+18` | Reverse float |
| `+1C` | Forward boost request; preserve unrelated player special-action bytes |
| `+1E` | Vertical boost request |
| `+1F` | Mounted trigger request |

Payload size is `0x30`. Ordinary neutral requests clear the managed action bytes. Reverse excludes simultaneous forward input; special-action modes reject incompatible combinations. Native call acceptance, logged latch transitions, displacement and visual behavior are separate levels of evidence.

The lead reference is resolved from FormID observed in live input dispatch `012C0550`, through lookup `005DE3B0` and virtual reference conversion `+230`. It must be loaded, match identity and be the vehicle currently driven by the player before retention/lease renewal. Raw reference `+24` is not interchangeable with every interaction handle.

## 7. Following, acceleration and recovery

The v57 candidate follows a bounded measured trail of the lead rather than steering directly to its current position. This is not a general obstacle planner. Position-derived velocity and yaw measurements damp steering, anticipate stopping and detect stalls. Calibration measures direction conventions and caches by live reference ID, handle generation and base model; caches are process-local.

Convoy following uses an elevation allowance `max(3, 0.75 * horizontalSeparation)`, replacing an overly restrictive fixed experimental height threshold. Moderate tunnel ramps were visually confirmed. This envelope cannot distinguish stacked floors or prove a clear route. Low-gravity behavior still needs tuning.

Recovery uses bounded forward assistance, reverse and optional short boost actions. Automatic vertical recovery requires exhausted earlier attempts, a fresh opt-in lease, near-stationary motion and appropriate separation; it is limited per recovery area and releases by time, movement or height. Native pulses are bounded to about 250 ms, quantized by updates. Player observations and charge readbacks confirm automatic activation, not reliable obstacle clearance. No headroom or grounded-state query is established. Interior/combat gating does not make outdoor roofs safe.

Gradual acceleration filters only the forward scalar in a copy of the player's payload, respects partial analog input and preserves other controls. It resets on reference changes, stale sampling and excluded actions. It does not rewrite the vehicle definition or establish that its tuning values are ignored by the engine.

## 8. Lights and effect sequences

Headlight routine `01314F50` has working call shape `(enabled, lightComponent, vehicleReference, onEffects, offEffects)`. Vehicle input component `+368/+398` provides the sequence arguments. Light type comes from runtime global `061DD7F8`; type 54 was recorded. Its layout differs from the input component: treating light-component `+0C` as an entity ID incorrectly blocked an earlier version.

Synchronization uses the extra vehicle's own reference, effects and live light component under validated ECS/write scopes. Lead toggle mirroring is visually confirmed. It observes toggles while the player drives the lead; that does not discover initial saved state. A light left on before save may remain on after reload. Initial-state policy and cleanup need explicit validation before claiming those cases fixed.

## 9. Mounted weapons and passengers' personal weapons

Explicit mounted firing was visually observed. Payload `+1F` drives component latch `+578`, then `0131D120`, checks `0128EE40` and trigger start `0128EE90`. Weapon container vtable `04C40318`, update `0128F6B0`, includes a six-entry action structure; entry 2 used vtable `04C40A58`, check `01296EB0`, start `01296EE0`, stop `012971A0`, tick `012971C0`. The observed dispatch continues through `012976D0 → 01297D20 → 012978E0 → 00AA5280`. Final ownership/damage attribution is not yet proven.

The automatic forward-support prototype checks a hostile live target, combat, distance 10–70 game units, heading within 4 degrees, height difference within 1.5 units and driver detection LOS. Known companions, player or lead ahead can block it. Requests last about 200 ms with at least 1.5 seconds between requests. The failed combat session issued zero firing requests: several visible targets were outside the cone; other cases failed detection or target acquisition. This cone is a controller eligibility rule, not a restriction placed on turret rotation.

Independent aim is the missing capability. Orientation routine `0131F6C0`, called conditionally at `0131D828`, reads a shared provider at global `061DD460`, retains its `+8` object and decomposes transform `+80` via `02BCDF60`. It applies weapon-data pitch limits and decomposes the vehicle-context scene transform separately, then updates selected node basis vectors at `+40/+50/+60` and node/parent virtuals `+248/+2E8`. Provider identity and a safe target-specific contract remain unverified. No shared provider replacement or guessed scene-node write is installed.

Papyrus `OnHit` registration and `Game.GetHealthAV()` can log aggressor, source, projectile and health changes. A health delta does not isolate its cause; detection LOS is not a muzzle raycast. A successful trigger is not proof of a projectile hitting the intended enemy.

A passenger's personal weapon could be made visible, but seated arm/torso pose remained unchanged. Convincing independent aim, fire and reload while preserving seated legs remain future work, outside the beta scope.

## 10. Configuration and gameplay behavior

Controller: Deploy vehicle, Convoy controls and Vehicle utilities. Preferences remain in native Gameplay Settings. Optional OSF Settings enumerates the same native option Forms, so edits share the game's values and notification path rather than a duplicate JSON preference store. Visibility/edit propagation still require in-game confirmation.

Automatic passenger boarding respects available compatible human seats. Convoy passenger layout chooses driver-only, a standard 2 + 2 distribution or filling extra compatible seats. One allied driver is implemented. Report unavailable seats; do not silently spawn more vehicles or dismiss followers.

Exit vehicles with player is enabled by default for new configurations. Managed occupants can leave while retaining a resume order; disabling it keeps allies seated waiting. Stop convoy explicitly releases the order. A controller pause suppresses new automatic starts without silently cancelling a running manual order. Saved choices remain authoritative. These newest lifecycle/menu changes still need grouped visual testing.

## 11. Future features

- Drive during dialogue: preserve player steering/throttle/braking while a compact response list uses mouse wheel selection and the interaction key to confirm. Use the game's actual dialogue branches/conditions; never auto-select answers. Separate input navigation, camera and movement layers, including gamepad conflicts and scripted quest locks. Not part of the current beta.
- Independent turret targeting and reliable mounted support; passenger personal weapons later.
- Enemy vehicles, scheduled A-to-B travel and allied POI routes.
- Trail-following caravans, broader path planning and multiple allied drivers.
- Alternate rear animations and broader capacity contracts.
- Improved low-gravity navigation and verified obstacle/headroom handling.

## 12. Validation and reproduction checklist

1. Keep the runtime version exact. Reject changed signatures rather than applying old addresses blindly.
2. Confirm plugin records, masters, own IDs and ESM/ESP alternative loading. Do not mix prototype and consolidated controllers.
3. Use two independent standard REV-8 references first; verify seat separation, driver pose, following and animated exits.
4. Test player dismount/remount, explicit stop, save/restart with occupants and unloaded-reference recovery separately.
5. Test light on/off mirroring separately from initial saved state.
6. Observe boost attempts in open terrain; retain logs and do not infer complete navigation from one recovery.
7. Keep mounted combat optional until target selection, aim, LOS, damage and ownership are validated.
8. Test custom geometry and every intended entry/exit side before advertising asset compatibility.

Local native fixtures verify scope/identity rejection, preservation of original arguments/results, reference balance, exception cleanup, key/pool checks, bounded input and recovery policies. Papyrus compilation and binary checks do not replace game/Creation Kit tests. Preserve a failing session's logs before another launch overwrites them.

The accompanying evidence ledger preserves historical experiments and failed hypotheses. It is research provenance; this document is the current consolidated interpretation. License notices and external-dependency policy are in CREDITS-AND-PERMISSIONS.md.

## Measured-trail candidate, 16 October 2026

Native v57 records up to 256 lead positions while a valid convoy input lease is active. Samples require at least 100 ms and approximately 1.5 horizontal units or 0.75 vertical units of movement. Stops do not consume the buffer. Lead reference/handle changes, stale input, discontinuities, nonfinite input and explicit order release clear history. The follower attaches only to the initial prefix, then advances in route order with a bounded per-tick step; it does not globally jump to a later crossing.

Steering uses a short look-ahead capped at a significant corner; throttle is based on remaining ordered arc and corner/heading limits. It retains roughly eight route units of lead separation, with an approach deadband. The existing control stabilizer, managed scopes and recovery remain. Automatic horizontal catch-up boost additionally requires at least 30 measured straight units ahead and small lateral error. No boost or reverse durations were increased.

Missing/stale/disconnected trails, excessive lateral/height error and loss of an unconsumed bounded-buffer segment lead to waiting rather than direct pursuit. Stop/restart or dismount/remount can collect a fresh history. The planner preserves height samples but does not query ground, vehicle width, dynamic obstacles, walls, overhead space or a walkable navigation mesh. Following a player path is not proof it is clear for another vehicle. Full local fixtures pass; gameplay behavior is not yet visually validated. The ESM/ESP and script identity are unchanged from v56, so this update itself introduces no new plugin migration.
