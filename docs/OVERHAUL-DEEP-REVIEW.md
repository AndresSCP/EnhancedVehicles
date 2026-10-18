# Vehicle Mod Overhaul WIP — detailed technical comparison

Reviewed 17 October 2026. External snapshot: [`d3641de021ec6608b7cd7d38a445835b643eca84`](https://github.com/creativityrebranded-cpu/Vehicle-Mod-Overhaul-WIP/tree/d3641de021ec6608b7cd7d38a445835b643eca84). A fresh fetch still points to that commit. Enhanced Vehicles comparison baseline: controller v63, native v65; our source changes from this review are native v66 and remain pending game validation.

## Scope and evidence

The repository contains 101 files under source/src, about 1.06 MB. Its CMake defines two separate DLLs. Review follows the SeamlessVehicle target, its shared services, integration headers and relevant SeamlessPlanets terrain/probe counterparts. The much larger planetary traversal implementation is not being imported or represented as fully audited. This is a functional source review, not a formal security/performance audit or a successful in-game reproduction of the external mod.

Read in detail: main_vehicle.cpp, vehicleflight.cpp, npcvehicles.cpp, realvehicles.cpp, vehicle_probe.cpp, hook.cpp and the interface/layout definitions. Examined the relevant paths in hoverui, controlmap, flight/flightmodel/flighttune, shipvehicle/shipmodel, api_client/core_api, modscene, logging/telemetry/crash and terrain/prefetch integration. Historical comments were checked against current executable statements; not every comment describes the current branch.

The user reports a video showing enemy/player vehicle combat. That is consistent with the implemented Pursuit path, but this review did not independently reproduce or view the video. README separates claimed tested features from built-but-untested features. In particular, it labels several gang/weapon/registry behaviors untested; source presence alone is not proof of reliable gameplay.

## Architecture and feature matrix

| Area | Enhanced Vehicles | External snapshot |
| --- | --- | --- |
| Package | One owned ESM, alternate CK ESP, own Papyrus, SFSE bridge | SeamlessVehicle DLL; optional connection to separate SeamlessPlanets core |
| Dependencies | Matching SFSE, CPE, official REV-8 roles; optional OSF settings presentation | Matching SFSE; optional Parked Vehicles and multi-follower mods; no CPE dependency in reviewed code |
| Additional vehicles | Explicit creation/recall of owned extras; active model used as deployment base | Current workflow registers/selects existing loaded vehicles; summon moves or flies an existing own vehicle |
| Ground movement | Verified input-component path with throttle ramp, steering and bounded recovery | Gear caps/physics modifications plus hover/autopilot model |
| Air movement | Normal vertical-boost input; relative-height test only | Integrated direct rigid-body hover, yaw/pitch/roll, strafe and speed control |
| Driver count | One allied driver | Six NPC job/pilot slots plus player pilot state; multi-gang behavior listed as untested |
| Passenger allocation | Standard human seat, explicit extra-role contract at markers 3/4, robot slot excluded, two-vehicle allocation | Focused on one vehicle per unseated follower; no equivalent extra-human-seat allocation contract found |
| Entry/exit | Animated entry/exit paths studied; current unwanted extra-passenger exit and occupied-save issues remain | Walk/direct/instant-seat fallbacks, movement fallback and held dismount; some teleport/snap behavior is deliberate |
| Combat | Explicit mounted firing verified; autonomous allied targeting/aiming incomplete | Escort/Pursuit switch, hostile actor setup, orbit/strafe and bursts aimed at the player |
| Weapon aiming | No verified articulated turret aiming | Projectile direction/velocity rewritten after firing; this does not implement visual turret articulation |
| Terrain | No active obstacle query; direct pursuit restored after strict-trail regression | Nine ground samples over three corridors, detour persistence, gear caps, trail fallback and climb policies |
| Recovery | Forward/reverse/vertical tiers, bounded attempts | Reverse, climb, repeated-contact handling, upright correction and teleport-to-target fallback |
| Lights | Allied headlights synchronization observed working | No equivalent synchronization path found in reviewed vehicle code |
| UI | Inventory/favorite gadget and native Gameplay Settings; optional OSF presentation | Startup-selected keys, injected HUD/scanner controls and layout editing |
| Health failure | Planned, not implemented | No health-to-immobilized vehicle state machine found in vehicle target |
| Validation | Native fixtures, Papyrus compiler checks and recorded feature-specific user observations | Source/log comments and README test-status declarations; no equivalent executable fixture target in its CMake |

## How its sustained hover actually works

The active rover model is in vehicleflight.cpp; the more general flightmodel parameters are not automatically the controlling implementation for every rover behavior. A vehicle physics-step detour identifies a body/motion pair, reads orientation/position and linear velocity, and maintains per-pilot shared state.

1. Ground samples and user/autopilot commands select a desired altitude or desired vertical speed.
2. Desired vertical speed is bounded by height error and climb/descent limits.
3. An integral term accumulates the vertical-speed error and is clamped to a finite range. It supplies the sustained lift needed to counter gravity/damping.
4. A proportional response adds lift or reduces it according to desired versus actual vertical speed.
5. Horizontal thrust, drag, strafe, braking and tilt effects are integrated with a bounded physics time step. Angular velocity is applied through a native physics writer; linear velocity is written to motion +0x60.
6. The boost-update hook suppresses ordinary boost resource/impulse in its managed contexts and sets graph boost visuals. Sustentation is therefore not limited to the vehicle's original boost-duration tuning.

This is a custom flight controller. Our observed height regulation uses ordinary booster requests, with the native vehicle resource system left authoritative. Its hover design could support an optional assisted-flight mode later, but copying it wholesale would change original vehicle behavior and modded resource tuning. A separate opt-in mode is preferable to silently replacing native boosters.

Source comments document earlier gravity-estimation feedback causing unwanted climbing and a lock-across-engine-call freeze; the current code switches to integral lift and releases its mutex around relevant engine calls. These are useful design lessons, not proof that all remaining synchronization/physics paths are safe.

## Navigation strengths and limits

The external controller looks at ground height ahead/left/right at three distances. It estimates climb demand, holds detours to avoid rapid oscillation, adjusts AI gear/speed, and only uses the player's recorded trail when obstructed or stuck. This differs materially from our withdrawn strict-trail target selection.

The samples are downward ground casts, not a complete vehicle-width collision corridor. The source explicitly describes a platform missed by its mask; collision responses then compensate with a temporary floor estimate. Overhangs, tunnel roofs, narrow geometry, unloaded ground and collision layers remain distinct problems. Unknown samples are skipped by Corridor and handled through separate speed policies. Its final teleport recovery can make an apparently robust convoy even when physical navigation fails. That fallback must be disclosed/configurable if introduced here.

We should adopt bounded, fresh terrain samples and turn hysteresis conceptually, after ABI/scope validation. We should retain our exact actor/reference/input identity checks rather than replace them with proximity-only physics routing. BodyNear in the external step routing uses a positional envelope during association; correctness with clustered vehicles needs its own validation.

## Enemy combat: what makes the demonstration possible

npcvehicles.cpp has a finite job machine: Boarding, Following, Settling, Leaving and Idle. Switching to Pursuit makes selected drivers hostile to the player, periodically reasserts combat and supplies an orbital target/AttackRun with yaw, strafe, throttle, pitch and gear channels. This is player-targeted pursuit, not yet a generic ally-versus-arbitrary-enemy targeting system.

Mounted triggering submits a handler-shaped input block through vehicle input routines and scopes the player-vehicle input gate for its own blocks. AimProjectiles then modifies projectile movement direction, velocity and rotation toward a predicted player position. Current canFire is range-based; the earlier nose-cone constant is not enforced in that branch. No equivalent friendly-fire/line-of-fire policy is established by that mechanism. Our ally support must independently select hostile targets, avoid friendly trajectories and decide whether projectile correction is acceptable gameplay.

## Lifecycle, performance and compatibility

The external DLL resets vehicle, NPC and probe state on load/world replacement. Its job and pilot arrays are bounded; the vehicle registry contains numeric IDs/metadata and clears on load. Numeric registry entries are not automatically new engine pointer handles. Its old-duplicate cleanup disables references rather than proving deletion/reclamation. Our explicit recall and naming-reference pruning remain valuable, but neither project has demonstrated a long-playthrough handle plateau.

No speed benchmark was performed. Fully native implementation does not itself prove lower cost. Registry scanning can traverse many cell references every two seconds; the current ScanCell ignores its budget parameter. Gang selection also scans loaded references when needed. Those broad discovery scans are unnecessary when our controller already owns a small fleet list. The external fixed probe slots and periodic/hysteretic navigation decisions are useful bounded-work patterns.

Keep our MinHook relocation support rather than copy the external raw-prologue trampoline utility, which is specialized to its selected prologues. Both DLLs touch overlapping input/fire/physics paths; simultaneous installation is untested. Do not combine them as a presumed compatible dependency stack.

## Independently checked native leads

Against the locally installed Starfield 1.16.244 executable, offline PE unwind-boundary lookup/disassembly confirmed the following function entries. No raw executable or disassembly is published here.

| RVA | Observed evidence | Remaining uncertainty |
| --- | --- | --- |
| 00583B70 | Entry exists; takes a context and point pointer, preserves third register argument and fourth float argument, reads a fifth float stack argument, returns a float; retains/releases an internal world owner | Live call scope/thread and complete filter semantics |
| 00583CB0 | Builds start/end points from input x/y and z + above, then subtracts length for the second z: consistent with vertical height casting | Collision layers, self-hits, missing-ground results and arbitrary geometry coverage |
| 01952530 | Entry/prologue agrees with the external dismount lead; actor/process/extra-data accesses exist | Full semantics, relation to our forced-exit stack, safe cancellation cases |
| 023CB260 | Vehicle-step entry/prologue exists | Complete body mapping, locking and compatibility |
| 013211F0 | Boost-update entry/prologue exists | Safe interception across vehicle variants and other mods |
| 02169100 | Update entry/prologue exists | Appropriate scheduling contract for our own probes |

These are stronger than an unverified copied address, but they are not runtime validation of a new callable API. No ground-cast or dismount-blocking hook was enabled by this review.

## Applied independently in native v66

- Recovery reverses for at most 1600 ms or three horizontal game units, instead of 800 ms/1.5 units. Existing throttle, failed-attempt requirement, vertical cutoff, one-area restriction and rearm policy remain. This addresses the previously reported lack of backing room; it does not add a rear collision sensor.
- The retained lead-vehicle cache now releases its owner when the same two-second observation lease expires. Existing retained snapshots remain valid independently. Replacement also releases the previous owner outside the cache lock.
- Fixtures verify longer bounded reverse limits, repeated stale-cache expiry, reversed time and reference balance with an outstanding snapshot. They pass locally. No flight-controller or enemy-mode code was copied.

## Prioritized next work

1. Independently validate read-only ground sampling under the correct engine execution context; log height-under-vehicle before applying a descent policy.
2. Separate native-booster following from any future custom assisted-hover mode.
3. Add a persistent disabled/repairable vehicle state after identifying authoritative vehicle health and hit delivery; see VEHICLE-DISABLE-RESEARCH.md.
4. Keep enemy vehicle jobs separate from party membership/driver selection; build arbitrary hostile-target acquisition before adopting pursuit combat behavior.
5. Investigate the spontaneous passenger-exit chain and occupied-save recovery using diagnostics; do not blanket-block forced exits.

The external repository has no tracked standard license file at this snapshot. Its author's public invitation to build on it is recorded in the earlier reference review. This work uses attributed research leads and independently authored changes; no external implementation was copied into our project.
