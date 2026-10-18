# Historical evidence ledger

Anonymized research provenance through 15 October 2026. Earlier version claims describe the state at that time and are not the current specification. See ENHANCED-VEHICLES-RESEARCH.md for the consolidated architecture, current keyword IDs and unresolved regressions. Historical plugin/role names are retained generically where needed to explain experiments; they are not installation instructions.

## Purpose and maintenance

This document records what this investigation adds to the materials we consulted, and what those findings make possible for a mod. It does not claim that these discoveries have never been published elsewhere. We have not performed an exhaustive literature or repository review.

Update this document when implementation, disassembly, logs or the user's observations change a conclusion. Correct earlier explanations rather than leaving contradictory statements as current guidance. Keep unsuccessful experiments when they prevent repeating a mistake. A successful local test is not equivalent to a successful in-game visual test.

The final shareable document should explain capabilities, evidence, reproduction requirements and remaining limits. It should not read as a list of prototype releases. This working version retains release numbers for tracing evidence.

All function names below are our descriptive aliases. They are not recovered Bethesda source-code names. Addresses are **RVAs relative to the running executable's image base**, for this runtime only. Structure offsets are also runtime-specific. They are not stable public APIs.

## What is possible so far

| Capability | Evidence and current limit |
|---|---|
| Deploy and recall an additional drivable vehicle while retaining the active vehicle | Visually confirmed through the controller. Tested with the supported multi-seat test vehicle. This does not establish that every vehicle model will work. |
| Order multiple human followers into distinct rear seats | Visually confirmed. Merely adding a Creation Kit marker did not make additional followers automatically occupy it. |
| Animate rear entry and exit | Visually confirmed after correcting marker entry masks in v25. |
| Hold followers in the additional vehicle and release them by order | Visually confirmed. The native driver hold uses a renewed, expiring order lease. |
| Give an NPC the sustained driver pose | Visually confirmed in v40, with a logged successful variable-context evaluation and restoration on session end. The successful implementation combines several changes; their individual necessity has not been isolated. |
| Drive the additional vehicle with native steering and accelerator input | Actual movement and following visually confirmed. This is a custom controller using native physical input, not evidence of an existing complete NPC vehicle-navigation system. |
| Automatically form a convoy when the player enters the lead vehicle | Implemented and tested with a selected or automatically chosen available follower. Full layouts depend on follower availability and compatible seats. |
| Synchronize the additional vehicle's headlights | Visually confirmed in v31 and retained afterward. It captures a lead-vehicle light toggle; initial state before any toggle is not independently read. |
| Offer gradual player accelerator input | Visually confirmed in v30. Optional setting; it ramps input rather than directly prescribing physical speed. |
| Recover from some stalls | Forward recovery visually confirmed in v32. A bounded reverse command executed in later logs; its effectiveness against obstacles is not separately established by a visual report. |
| Follow across moderate elevation changes | Visually confirmed in v42 in the tested New Atlantis tunnel, with rises and falls. General terrain navigation remains unimplemented. Earlier convoy code incorrectly reused a fixed 2.5-unit vertical rejection. |

Still open: obstacle avoidance, route selection through tunnels or roads, safe recovery around cliffs and different floors, broad vehicle compatibility, autonomous enemy traffic, allied travel between POIs, a trail-following caravan, and persistence across loading transitions. These are future capabilities, not demonstrated outcomes.

## Existing interfaces versus reverse-engineered findings

Papyrus functions such as `SnapIntoInteraction`, furniture/sitting queries, animation-variable accessors, keyword operations and timers are existing interfaces. Gameplay Settings integration and a favoritable inventory controller are implementation choices, not new engine discoveries.

The additional information obtained here concerns native call paths, data layouts, temporary animation evaluation context, seat-selection behavior, physical-input application and light component handling. The supplied Creation Kit PDF describes the Animation Previewer and the relationship between subgraphs and posture on pages 19–20; it does not provide these native layouts or vehicle-specific procedures. It was useful context, not a direct solution to the driver pose.

## Seating and natural interaction

In the tested furniture layout, the base object's marker count is at `+0x270`, marker array at `+0x278`, enabled-marker mask at `+0x264`. Marker entries have a 40-byte stride; the marker keyword pointer is at entry `+0x10`.

Giving every extra seat the normal companion keyword caused front-seat selection or overlapping occupation. The successful prototype reserves rear seats with separate owned keywords and scopes marker selection to the requested actor and vehicle. It also temporarily excludes reserved rear occupants and the assigned extra driver from normal front-companion selection, removing only exclusions it added.

Marker IDs in this supported model: pilot 0, normal human companion 1, additional rear positions 3 and 4. This is a model-specific arrangement, not a universal seat convention. Vasco's special position and arbitrary vehicles need their own inspection.

Natural entry uses the vehicle interaction constructor at `0x01315260`, with actor and target handles and the non-forced, zero-retry path. Full preparation at `0x01A04C30` matters: immediate placement through the outer snap path can bypass preparation and animated entry. The rear entry implementation preserves the original complete preparation wrapper.

The rear animation fix in the seat plugin changed the tested marker entry masks from `0x14` to `0x04` and `0x18` to `0x08`: remove the extra “Other” entry direction and retain the intended right/left direction. Distinct seats and exit animation alone had not proven natural entry before this change. Preserve the successful v25 marker patch while changing other systems.

A driver's immediate exit can be intercepted only for the managed pilot, matching vehicle handle and marker 0. Holding expires unless the controller renews its owned order. Cancellation requests the normal exit and clears physical input. Existing actor behavior outside this scope remains original.

## Driver pose: the important result

Being logically seated in marker 0 did not give the NPC the correct seated pose. The NPC could enter with the driver's entry animation, briefly appear at the wheel, and then settle into the passenger pose.

Official rover metadata contains two different animation keys:

| Metadata | Decimal filename key | Hex key |
|---|---:|---|
| NPC | 777300086817692085 | `0AC9864F86A619B5` |
| Player | 13247197233673542069 | `B7D77FB286A619B5` |

The NPC animation-file table lacks the `rover/player/Idle` clips present in the Player table. The two sets share lateral entry/exit clips. `FurnitureRover_Wrapping.agx` chooses NPC/Player branches using `iIsPlayer`.

Experiments narrowed the problem:

1. Setting `iIsPlayer=1` after the NPC sat down succeeded according to readback, and restored to 0 at exit, but did not fix the visible pose.
2. Substituting the animation resource query during preparation did not execute in the scoped path in v35. Absence of the event is a reachability failure, not proof that the resource hypothesis was false.
3. Passing the player's animation provider directly to candidate selection did execute in v37, but the selected key remained NPC.
4. Replacing the NPC key in marker-0 candidate results produced and maintained the Player key in v38. The visible pose still failed.
5. The scoped companion-transition function had not been registered in the current hook table. v39 connected it; logs confirmed that the branch suppression executed. The visible pose still failed. Earlier assumptions about that function being active were incorrect.
6. v40 scoped the **actor used to evaluate prepared animation values**, while retaining the NPC's destination graph. The user then confirmed the driver animation worked.

### Variable preparation path

The inspected path is:

```text
019C47F0 → 01AFA090 → 01AFA460 → 01AFAB40
         → 01B003D0 → 01AF8F10
```

`019C47F0` clears previous prepared variables, obtains binding data and prepares the process's pending array. `01AFAB40` builds a temporary evaluation context. The relevant context fields are provider `+0x08`, actor used for value evaluation `+0x18`, and the variable-list input at `+0x20`. Other fields carry bindings and results.

`01B003D0` visits animation graphs and invokes `01AF8F10(context, destinationGraph)`. **The actor used for evaluation is distinct from the destination graph.** The successful v40 prototype temporarily changes only context `+0x18` to the loaded player during the managed pilot's preparation callback. The graph argument still belongs to the NPC. The original callback executes once; the actor field is restored in `finally`, including native exceptions. Output fields are preserved.

This is not a change to the NPC's FormID, player globals, control ownership or persistent identity. It is currently a broader value-evaluation substitution within one preparation scope, not a demonstrated override of only a named `iIsPlayer` property. The exact minimal set of pose changes remains an ablation task.

Recorded eight-byte prepared-variable elements differ between player and NPC. Their full semantic layout is not yet established; do not label them `iIsPlayer` IDs or copy them blindly.

### Candidate result layout

In the inspected preparation path, output rows have a 32-byte stride, marker number at row `+0`, candidate count at `+8`, candidate array at `+0x18`. Candidate records have a 64-byte stride. Native preparation separately copies the candidate key at `+0x28` into high-process `+0x4F0`, and auxiliary data at `+0x20` into `+0x440`.

The prototype replaces only the exact NPC rover key in local marker-0 results. It preserves geometry, auxiliary data and other keys; rear markers are excluded. This layout should be revalidated when porting, rather than treated as a general animation API.

## Native physical input

The successful bridge finds the vehicle's ECS component using a full key:

```text
key = (componentType << 48) | (vehicleFormID << 16)
```

For this runtime, vehicle input component type is `0x77`, read from the runtime type global. The lookup path uses the game-systems manager, manager `+0x268`, pool/index output and the full entry key. A matching FormID alone is insufficient.

The manager is obtained through `0x023FF640` and game systems `+0x8B0`; owner at manager `+0x238`. Read scope enters through `0x02404230`; lookup through `0x024105D0`. Pool/index are returned at result `+0x10/+0x18`. The prototype rejects sentinel index `0xFE0` and validates count. An entry is addressed through a pool offset table; its key is at `+0x10`, component at `+0x20`.

**Write-entry ABI matters:** `0x02404330` takes two arguments: page allocator and pool header. The header is pool `-0x20`; allocator is header `+8`. An earlier wrong ABI caused a crash. Use the same native write discipline as the input-dispatch path, retain the owner during access and unwind read/write scopes on all exits.

The tested input component has identity at `+0x0C`, link at `+0x10`, and a temporary gate at `+0x579`. The prototype validates its vtable and scope, temporarily enables the gate, calls original input application at `0x01324650`, and restores the gate. Component pointers do not escape the query.

The input payload is 0x30 bytes:

| Offset | Observed use |
|---|---|
| `+0x10` | Steering float |
| `+0x14` | Forward accelerator float |
| `+0x18` | Reverse input float |
| `+0x1C` | Special-action flags; leave unchanged for player input |

A native input call accepting a payload is not sufficient evidence of movement. The project separately recorded application, position changes and visual following. Autonomous commands do not fabricate booster input. The bounded reverse recovery excludes simultaneous forward accelerator input.

## Obtaining the actual lead reference

The original passive runtime observer did not supply a usable lead reference. v30 resolves the FormID observed in live input dispatch (`0x012C0550`) through native form lookup (`0x005DE3B0`) and the returned form's virtual `+0x230` conversion, corresponding to the runtime's reference conversion path.

The result must have the expected FormID, loaded 3D and match the player's current pilot handle. Only then is it retained and its lease renewed. A stale or unavailable reference is rejected. This change enabled the previously blocked accelerator filter, following and light synchronization.

Raw reference `+0x24` is not assumed to be the same handle accepted by every native function. The implementation uses the verified handle-resolution path for interaction ownership. Spawned FormIDs, plugin load-order IDs and addresses are not portable identifiers.

## Headlights and effect sequences

The inspected headlight function is `0x01314F50`, with working call shape:

```text
(enabled, lightComponent, vehicleReference, onEffects, offEffects)
```

The vehicle input component supplies effect-sequence arguments at `+0x368` and `+0x398`. The light component type is read from runtime global `0x061DD7F8`; type 54 was recorded in the test. It is a different component layout from the input component.

The light component is array-like. An early check incorrectly treated `lightComponent+0x0C` as an entity ID, preventing application. v31 removed that assumption while retaining the full ECS entry-key check, pool bounds and native write scopes. It then applied on/off using the **additional vehicle's own** reference, light component and effect-sequence arguments. The user confirmed visible synchronization.

The controller records lead toggles only while the player drives the reference. This establishes toggle mirroring, not initial-state discovery before the first toggle.

## Motion measurements and current controller limits

The inspected reference pose accessors read position `+0x8C/+0x90/+0x94` and yaw `+0x88`. Direction conventions were calibrated from measured travel and turning rather than hardcoded. Cache identity includes reference ID, raw handle generation and model base; the cache lasts only for this process.

Following uses the lead's current position with a trailing offset, not a stored route. The current controller adds bounded steering, slower turns, position-derived approach anticipation and short stall recovery. These are prototype algorithms, not recovered Bethesda navigation parameters.

The fixed 2.5-unit height rejection belonged to our short experimental controller, not evidence that Starfield cannot navigate slopes. Reusing it for continuous convoy following could stop the ally on ordinary ramps. v42 gives convoy height allowance `max(3, 0.75 * horizontalSeparation)` and logs target height/delta. This is a slope envelope, not a terrain query; vertical-floor ambiguity and obstacle routing remain unresolved. The user confirmed successful following through the rising and falling New Atlantis tunnel after v42. Exact terrain geometry and broader slope coverage remain unmeasured.

Gradual player acceleration copies the original payload and changes only forward accelerator input. It preserves steering and other bytes, bypasses reverse/special actions, respects partial analog input and resets on vehicle changes or stale samples. It does not demonstrate that the engine ignores all vehicle tuning settings. That wider claim remains unproven.

## Reference layout notes

| Object/field | Observed role in this runtime |
|---|---|
| Reference `+0x28` | FormID |
| Reference `+0x98` | Base object |
| Reference `+0xB0` | Loaded 3D pointer |
| Actor `+0x228` → process `+8` | High-process data |
| High `+0x424/+0x428` | Desired/current interaction handles |
| High `+0x4F8/+0x4FC` | Desired/current marker indexes |
| High `+0x4F0` | Prepared animation key |
| High `+0x440` | Prepared auxiliary pointer |
| High `+0x350/+0x330` | Pending/previous variable containers used in the entry path |
| Actor flags `+0xFC & 0x3800` | Observed entering `0x2800`, seated `0x3000`, exiting `0x3800` states |

These observed masks are not a complete actor-state specification. Reference retention and release follow the runtime's native implementation, including its composite count; substituting a guessed ordinary reference-count increment is unsafe.

## Evidence index and next work

### Requested next capabilities: boost, obstacle recovery and fire support

The user wants the allied vehicle to keep following while providing fire support, and potentially use boost/jump to clear obstacles. These capabilities are not implemented or proven yet.

Separate three boost tasks: identify the actual native action inputs and resource behavior; use short forward boosts to catch up on clear ground; then consider vertical boost/jump with terrain and landing checks. Reusing an input channel does not establish when a jump is appropriate. Current autonomous payloads leave special-action flags zero; the exact boost/jump mapping still needs inspection and controlled capture.

Obstacle jumping requires more than distance to the lead. The current controller does not measure obstacle height, available headroom, ground continuity or landing clearance. It cannot yet distinguish an obstacle that should be driven around from one that can be jumped safely. Stall recovery remains separate from route selection.

Combat currently terminates or rejects the convoy in controller checks of player/driver `IsInCombat`, and the pose scope also excludes a driver in combat. Fire support therefore needs an explicit combat policy and compatible pose/seat ownership, not merely a trigger call. Preserve follow mode, target acquisition, aiming, line-of-fire checks and cessation of fire as distinct responsibilities.

Vehicle-mounted weapons and passengers firing personal weapons are different integrations. The user clarified the current goal: the driver uses the mounted vehicle weapon, while the copilot provides support with their personal weapon. Both are requested features. Personal-weapon firing while seated is not yet demonstrated and not proven impossible. Do not assume that the supported ATV or every vehicle weapon mod exposes the same firing mechanism. No autonomous firing or aiming has been established.

The visually confirmed driver idle does not establish dynamic upper-body aiming. Inspection of the extracted `FurnitureRover_Wrapping.agx` finds `AimAtLocation`, `LookAtLocation`, `bNoAim`, `bAimActive`, and nodes named `LookAt_Head` and `LookAt_Chest` (around lines 1234–1428). These are concrete investigation leads, not proof that writing these variables will animate the seated NPC or aim a mounted weapon. Determine their bindings, active graph branches and coordinate conventions before modifying them. Independently verify whether the mounted weapon aims separately from the actor torso; personal-weapon aiming remains a different problem.

Initial input-path inspection: native input application at `01324650` tests bytes `+1C`, `+1D`, `+1E` and `+1F` separately. The `+1F` branch uses a latch at component `+578`, calls `0131D120` on activation, and invokes stop callbacks when released. The `+1C/+1D` paths access component state `+240` and scalar `+238`; the `+1E` branch updates component `+241` with state/resource checks. Input `+1C` should not be described as a fully mapped single flag word. The grouped v43 player capture below now provides action correlations; `+1D` is still unobserved and unlabeled.

Suggested implementation order: controlled action-input capture; forward boost catch-up; convoy behavior during combat; weapon-specific fire support; obstacle sensing and route decisions; bounded jump/landing behavior. Validate multiple tasks in grouped sessions where practical.

Native v43 adds a passive player action capture before accelerator filtering. It checks the native vehicle input-component identity and player pilot ownership, copies the existing 0x30-byte payload, and records raw bytes plus uninterpreted float views. Sampling is at most every 100 ms except special-byte +1C..+1F changes, which are recorded immediately to distinguish press and release. Capture is bounded to five minutes from the first validated player-driver sample and 2000 records per process. It requires neither a driver probe nor an active convoy. Existing native input is forwarded through the original v42 behavior; no special action is synthesized. Local tests exercise sampling, press/release recording, duration and record limits. The grouped procedure is recorded in `outputs/PRUEBA-ACCIONES-V43.md`; results follow below.

The completed v43 test yielded 690 samples over 125.297 seconds for player-driven reference `FF0AAA24`. Following the requested action sequence, byte `+1F` pulsed seven times while forward/reverse input was zero (first cluster at 8.266–10.422 seconds; second at 85.531–87.547 seconds), correlating with mounted firing. Byte `+1C` was held for 0.937 and 0.922 seconds at 95.719 and 98.031 seconds with forward input 0.982503, correlating with forward boost. Byte `+1E` was held for 1.235 seconds at 103.109 seconds with zero forward/reverse input, correlating with vertical boost. These are player-action mappings supported by the prescribed sequence and native branch behavior, not evidence that autonomous firing/boost already works. `+1D` remained zero. No independently identified aiming direction was obtained: offsets `0..8` remained zero, `+0C` switched between 0 and 1, `+24` stayed at FLT_MAX, and `+28` varied similarly to steering. Do not label these fields as aim coordinates without finding their producer/consumer. Byte `+20` is also read as a Boolean by `01324650` (calls `0131A1E0`/`0131A370`), so viewing its containing DWORD as a float is only a diagnostic representation.

Copilot animation lead: extracted `FurnitureFireWeapon.agx` references `GunActions`, has an `IdleSitting` transition and `IdleChairSitting` enter event, and includes the `UpperBody_BoneWeights` animation tag. This supports investigation of an existing seated/gun graph composition; it does not establish compatibility with rover passenger graphs, current seat ownership, equipped weapons or NPC combat packages. No global graph replacement has been installed.

Further graph inspection resolves that composition: inside `FurnitureFireWeapon`'s `Weapon Furniture` blend tree, `ActionBlend` is an `NT_COPY_BONE_WEIGHTS` node controlled by `BoneWeightsAction`. Its base input links to the embedded `Idle` state machine, its layered input links to the action switch (whose Actions branch references `GunActions`), and its Walking/Turning and Jumping weight inputs link to `UpperBody_BoneWeights`. This is stronger evidence than merely finding an unused tag: the graph wires gun actions over a seated base. The root enters this blend tree with `IdleChairSitting` and accepts `IdleSitting`. Link node identifiers in this graph are one-based; the exported topology's `index` field is zero-based. These details still do not prove the NPC AI will issue gun actions while held in a rover seat.

`Furniture.agx` lists `FurnitureFireWeapon.agx` in a dynamic graph reference switched by `LoadFurnitureGraph`. By contrast, `FurnitureRover_Wrapping.agx` includes an event controller listening to `LoadFurnitureGraph`, with `bIsFurnitureExit` as logic variable and `Discard Events=True`. Consequently, a generic request to swap the furniture graph is not a reliable rover integration strategy. The active condition, graph lifetime and seat-specific animation bindings must be respected. A copilot experiment should layer actions onto the owned passenger branch and preserve entry/exit and lower-body seat clips, rather than replace the graph globally or reuse the driver `iIsPlayer` change for passengers.

Further native routing: `0131D120` obtains a nested object through input component `+20 -> +20`. `0128EE40` visits that object's pointer array (count `+68`, data `+70`) and calls virtual `+28`; `0128EE90` visits the same array and calls virtual `+30`. The firing-release path in `01324650` calls virtual `+58` on those entries. Exact entry types and shot attribution remain unidentified. A full activation-loop listing was produced across adjacent unwind fragments: the original enclosing-function dump truncated this routine and was not sufficient to infer its complete loop.

The input component copies payload `+08..+0F` to component `+57C..+583`. Consumer `01321840` treats component `+57C/+580` as two scalar inputs, combines them with vehicle/body basis vectors and calls `0235E530`; this is a physics path, not established turret aiming. Both source scalars were zero throughout v43. Vehicle update `0131D5F0` builds a temporary context via `0131B0D0`: it carries vehicle reference at `+100`, linked runtime object at `+108`, definition at `+110` and reference-derived scene pointer at `+118`. It calls `0131EF10` and conditionally `0131F6C0` with that context, then the nested weapon object virtual `+10`. These routines are investigation leads for runtime weapon transforms, not callable aiming APIs with a proven contract. Do not supply guessed contexts or reuse pointers beyond their native scope.

Evidence added: `work/seated-weapon-graph-topology.json`, `work/weapon-routing-report.json`, `work/weapon-activation-loop.txt`, `work/weapon-function-01321840.txt`, and native listings for `0131B0D0`, `0131EF10`, `0131F6C0`. `GunActions.agx` was also extracted from the base animation archive for further per-actor action/weight analysis. No changes to game behavior were installed during this routing review.

### v44 bounded forward boost and passenger weapon check

Native v44 extends the existing verified input bridge with an explicit forward-boost Boolean that writes only payload byte `+1C`. It rejects boost with reverse, throttle below 0.9 or steering magnitude above 0.2. It retains the original native call, including the engine's resource/state checks. Stop/cancel paths send neutral input with the boost byte cleared. Autonomous jump and firing remain zero.

Experimental convoy eligibility requires calibrated, owned seated driver scope and a valid loaded lead, a valid recent motion measurement, separation strictly between 40 and 150 game units, heading error below 0.12 radians, steering below 0.2, throttle at least 0.9, measured speed from 2 to below 22 units/sec, yaw rate below 0.15 rad/sec, and leader height separation below 3 units. Assistance/reverse must not be active. A pulse requests at most 350 ms; the existing 100 ms control tick quantizes release. A pulse also ends after 8 units of measured horizontal displacement, 1 unit of height change or loss of eligibility. Each session permits at most three requests, separated by at least eight seconds. These kinematic bounds do not establish obstacle visibility or a clear path. Actual autonomous boost effect awaits the grouped v44 test.

Local fixtures verify boost/neutral payloads reach the original native callback within the existing write scope while preserving the gate, references and read/write balance. Tests cover invalid simultaneous input, deadline release, heading loss, cooldown, pulse count, stalled motion and height separation. No guessed energy field is written.

Controller v44 adds hidden global `RV8VehicleFleet.CopilotWeaponProbe`. It checks the selected loaded human passenger's seated vehicle scope, excludes the tracked driver and actual marker-zero user, requires an equipped weapon and no combat, then calls existing `DrawWeapon` and samples for up to eight seconds. It logs weapon-drawn state, equipped weapon, seat/furniture and graph variables. It requests sheathing afterward only if the weapon was originally sheathed and the actor remains loaded and out of combat. Missing graph-variable readbacks are not proof that a variable exists in the active graph; Papyrus diagnostics and visible behavior must be checked. This does not issue a firing command or replace a graph. The visible controller menu remains unchanged.

Native v44 also adds bounded, read-only weapon-object routing logs within validated player input capture. It reads the nested weapon container and up to eight entries (rejects count above 16), recording module-relative vtable/update/check/start/stop addresses for offline type/path identification. It never invokes these entries or retains their pointers. The capture budget is twelve observations per process. Two short player-fired bursts in the grouped test can provide those identities without repeating the full v43 input mapping.

### v44 observed weapon state and v45 hand-transition experiment

The actual v44 passenger check is in rotated `RV8VehicleFleet.1.log`, archived as `outputs/RV8VehicleFleet-v44-prueba-sesion.log`. After reloading, `.0.log` only contained a new initialization. Inspect rotated sessions before concluding that a test did not run. The global console call requires quoting: `cgf "RV8VehicleFleet.CopilotWeaponProbe"`. A suggested numerical CQF fallback failed because the plugin's runtime index changed (53 to 54). Use filename-based form resolution and quoted global calls rather than retaining a previously observed runtime index.

Passenger `FE00B007` in vehicle `FF08CAC6` retained sit state 3, equipped Grendel_UC, `IsWeaponDrawn=true` already before the check, `iIsPlayer=0`, `bNoAim=false`, `bAimActive=true` and `BoneWeightsAction=1`. The user observed the weapon, but the usual seated posture without the intended upper-body action. The data therefore does not show a successful draw transition or aiming animation. The original 16 samples took approximately 26 seconds including script overhead despite the eight-second notification.

Further inspection of `FurnitureRover_Wrapping`'s `Hand IK On/Off` state machine finds explicit transition conditions: `bEnableHandIK=0` enters `IK Disabled`, incrementing `bNoLeftHandIK` and `bNoRightHandIK`; `bEnableHandIK>0` selects the alternate state. The wrapper initially assigns `bEnableHandIK=0`. Controller v45 uses this existing transition via a scoped integer change from 0 to 1, retaining the previous value and restoring only its owned value afterward. It never directly zeros the nested disable counters. Cleanup is deferred if the actor unloads and retried by the existing fleet timer; initialization also requests cleanup. The check remains limited to the selected seated non-driver, excludes combat, and samples against a real-time deadline with fewer logs. No global animation graph has been replaced. In-game effect remains unproven pending v45.

The player-mounted weapon capture resolved container vtable `04C40318`, update `0128F6B0`, and six entries. Entry 2 has vtable `04C40A58`, check `01296EB0`, trigger-start `01296EE0`, trigger-stop `012971A0` and tick `012971C0`. Start changes state `+10` and invokes `012976D0` when not blocked by `+2C`; it sets held flag `+38`. Stop resets state/timer/count fields and clears `+38`. The container tick iterates entry virtual `+78`, passing vehicle reference and delta time. These observations identify a native action-state lifecycle, not proof of projectile emission or correct NPC attribution. Leaf methods without unwind entries require bounded disassembly; failure of the earlier unwind lookup was not evidence of missing code.

No driver/caravan or forward-boost request was recorded in the archived v44 session, so actual booster behavior remains pending rather than classified as a failed implementation. Mounted identification is complete enough for further offline tracing; a repeat of those player bursts is unnecessary for the next hand/boost test.

### User-provided game settings database

The user supplied a ZIP containing `Starfield_Game_Settings_Database_1-1.xlsx`. A preserved local copy is under `work/game-settings-database`. Its sheets are `Creation Kit All Settings` (8706 rows including header), `Starfield.esm Settings` (2313 rows including header) and `Category Summary` (1000 worksheet rows, not a count of meaningful categories). The first sheet separates name/type, override status, claimed effective value, master value, engine default and FormID. Blank values must remain unknown; the workbook's effective-value column is not a measurement of the user's currently modded running game. There is no demonstrated source runtime match merely from the archive filename.

Thirteen selected names were independently found in this runtime's executable and in setting initialization routines. Float constructor `002D65E0` writes the name pointer at object `+18`, initial float at `+08`, and copies the value to `+10`. Boolean constructor `00473630` uses the same layout, writes the Boolean byte at `+08` and copies it to `+10`. Consumer instructions observed here read the `+08` value. Settings are constructed at startup, explaining why searching the on-disk executable for already initialized absolute string pointers found no objects. Registration and downstream read paths, rather than string existence alone, provide stronger evidence.

| Setting | Verified engine initializer | Evidence/use in this runtime |
|---|---:|---|
| `fVehicleMinAccelInputToToggleOnBoosting` | 0.5 | Object RVA `05F04498`, value `+08` read in input application `01324650`. |
| `fVehicleMinBoostChargePercentToStartBoosting` | 0 | Object RVA `05F04478`, charge checks in `01324650`; this workbook row 6070 is blank. This is an initializer value, not proof of unlimited boost. |
| `fVehicleBoostGravityAdjustment` | approximately 0.8 | Registered at object RVA `05F044B8`; no downstream use established by this partial scan. |
| `fVehicleVerticalBoostGravityAdjustment` | approximately 0.8 | Read in vehicle routine `013211F0`. |
| `fVehicleBoostCeiling` | 10 | Read in `013211F0`, including comparison of a position height to a stored height plus this setting. |
| `fVehicleAutoHandbrakeSlope` | 20 | Read in `0131B730`; changing this is not yet validated as a fix for convoy slopes. |
| `fVehicleChassisObstaclePaddingSize` | approximately 0.6 | Read in `00E6DF90`, associated with geometry/collision bounds. It does not by itself supply a route planner. |
| `fVehicleAvoidanceLookAheadTime` | 2 | Registered; exact downstream behavior remains under investigation. |
| `fVehicleAvoidanceMaxDistance` | 64 | Read in `002D5330`, with reference-handle resolution and position-distance comparison. |
| `fVehicleAvoidanceCowerTime` | 4 | Read in `002D5200`, which accesses the actor process at `+228` and issues actor-process calls. Evidence points to actors reacting to vehicles, not established rover obstacle avoidance. |
| `fVehicleWeaponForceTorqueMaxDeflectionAngle` | 5 | Read in known vehicle runtime routine `0131D2C0`. Do not equate weapon-force/deflection behavior with target aiming. |
| `bCombatFurnitureShouldReload` | false | Registered Boolean, object RVA `05EDEE10`; workbook row 1207 is blank. Downstream use and applicability to rover copilots are unproven. |
| `fFurnitureEntryHolsterDistance` | approximately 3.9291 | Read in `019DAC80`; workbook row 4425 gives the master override 3.929063. This concerns holstering on entry, not proof that firing while seated is allowed. |

The database also exposes air-control, camera, exit/collision, spawn-placement and passenger dialogue settings worth keeping as an investigation index. Passenger dialogue cooldown names for aiming/weapon kills do not prove that the passenger fires the gun. Vector-looking names whose scalar/string variants have blank cells must not be assigned guessed floats. No global settings or deployed mod files were changed during this review, so the pending v44 test remains the same.

Evidence: `outputs/GAME-SETTINGS-DATABASE-REVIEW.json`, `work/vehicle-settings-native-verification.json`, `work/disasm-002D65E0.txt`, `work/disasm-00473630.txt` and the referenced `work/setting-function-*.txt` listings. The native reference scan covers selected instruction forms and is not an exhaustive absence check.

Working source: `work/native-trace-v40` is the successful pose baseline; `work/native-trace-v42` includes the height change; controller `work/controller-v41` includes reduced boarding intervals. The seat patch is the confirmed v25 patch. All paths here are project-relative for internal traceability, not external downloads.

Key preserved evidence:

- `outputs/RV8NativeTrace-v31-prueba.log`: light application and following.
- `outputs/RV8NativeTrace-v37-prueba.log`: provider substitution executed, NPC key remained.
- `outputs/RV8NativeTrace-v38-prueba.log`: Player key selected and maintained, visual pose failed.
- `outputs/RV8NativeTrace-v39-prueba.log`: transition suppression executed, visual pose failed.
- `outputs/RV8NativeTrace-v40-prueba.log` and fleet companion log: successful variable-context callback, neutral input and end-of-session restoration; user visually confirmed the pose.
- `work/pilot-metadata`: extracted official NPC/Player metadata for local comparison.
- `outputs/RESULTADO-POSE-V40-EMBARQUE-V41.md` and `outputs/SEGUIMIENTO-DESNIVEL-V42.md`: current pending timing and slope validation.

Before publication: isolate which pose interventions remain necessary; resolve plugin forms dynamically rather than retaining the prototype's `FE09A844` load-order-specific model check; document supported models and follower counts; test state cleanup across saving/loading, combat, unloading and cancellation; distinguish proven reverse recovery from applied reverse commands; validate boarding timing and elevation behavior. Record newly discovered possibilities here as the work continues.



### v45 observed results and v46 driving priority

The user confirmed the passenger retained the usual seated posture and tentatively observed allied boost. The hand hypothesis is not supported by this test: bEnableHandIK was already 2 and both disable counters were 0 before the check, so the scoped 0-to-1 change never ran. The hand-enabled log label denotes a sampling phase, not proof of modification. Aim offsets changed slightly, without visible upper-body action. Personal-weapon work is postponed at the user's request; allied driving is now the priority.

Native v44 recorded three boost request/release pairs during this v45 session. Pulses 1 and 2 lasted approximately 218 and 226 ms, with measured speeds rising from 14.260 to 17.963 and 11.646 to 20.921 units/sec. This supports the command path and agrees with tentative visual observation; it does not isolate boost from ordinary acceleration. Pulse 3 lasted approximately 106 ms without a speed increase. The three-request session cap remains unchanged.

One reverse recovery occurred at 21:44:42.357, followed by later stall assists without a second reverse. The once-per-session latch explains why recovery could not be reused at another obstacle. Native v46 rearms only after at least six horizontal units from the previous recovery origin and fifteen seconds after its pulse deadline. Time alone cannot reset a stationary blocked vehicle. Existing 800 ms, 1.5 horizontal-unit and 0.75 height-unit pulse limits remain. Tests cover stationary, insufficient displacement, insufficient cooldown, invalid pose and backwards time. In-game effectiveness awaits v46 validation. This remains positional following and bounded recovery, not obstacle-aware path planning.

Offline mounted tracing also resolved queued callback 01297D20 -> 012978E0 -> 00AA5280. The latter validates a loaded reference and distinguishes actors by type 4B before accessing actor process 228. Reference resolution and retained instance lifetimes are visible. This path is not called by the mod; aiming, emission and NPC attribution require further analysis. Listings are preserved in work/weapon-dispatch-*.txt.

Controller v46 retains convoy intent when the player dismounts rather than ending the session. Native lead validation already emits neutral movement when the player is not driving the lead; the driver heartbeat continues. Actor/vehicle loss or failed seating pauses the session while retaining the requested driver/vehicle for a later loaded, eligible retry. Initialization releases stale runtime ownership while retaining convoy intent. An explicit stop clears intent and suppresses automatic restarts until a manual start. These control changes compile successfully; save/load, pause/resume and explicit-stop behavior require in-game validation. Combat no longer clears the order automatically; seated combat behavior remains unvalidated.

The user prioritizes standard REV-8 pairs and role-based compatibility across models. Current hardcoded multi-seat test configuration guards and rear-only allocation have been inventoried; no generic compatibility is claimed yet. The separate author-facing draft docs/VEHICLE-MODDER-INTEGRATION.md documents indices, actual keyword Forms, tested entry flags, ownership cleanup and current limitations. Future capacity checks must distinguish human seats from Vasco-only positions, notify shortages and leave deployment/dismissal decisions to the player. Extending beyond one allied driver requires changes to the single-driver native registry.

### v46 validation and v47 standard vehicle integration

The user visually confirmed that the driver remained seated when the player dismounted, resumed following on remount, and was released to walking follow by Stop convoy. Recovery helped with some obstacles but not all geometry. Native logs show four reverse requests and four rearms in the 22:43–22:50 session. Several boost requests also occurred. These observations support persistent orders and reusable recovery, not general obstacle navigation. The user specifically identifies vertical boost as a player's escape technique; autonomous vertical boost remains pending.

v47 removes all multi-seat test configuration/base-ID gates from the native bridge and controller. Native role checks validate the enabled driver marker, bounded marker array and role keyword editor IDs through the verified keyword getter; the controller validates vehicle keyword and exact driver/passenger Form identity through CPE. Physical component validation remains intact. Managed passengers now include marker 1 (standard companion), plus canonical extra-human markers 3/4. Marker 2 is never allocated to humans. Seat holding and temporary front-selection exclusion now protect a managed passenger when native selection targets another vehicle. Tests verify the front natural constructor, duplicate-seat rejection, cross-vehicle selection protection and reference balance, along with prior physics/pose/light/recovery fixtures.

Deploy now copies the active vehicle base on each controller opening. A standard REV-8 active vehicle therefore supplies the extra model without installing multi-seat test configuration. Boarding checks actual marker count and exact seat keyword before occupancy queries, skips absent seats and reports shortages. Failed natural entry releases the provisional order. The full-extra layout derives its requested passenger count from free compatible front/rear slots rather than assuming two rear seats. The native registry still supports one allied driver; additional parked vehicles can exist, but multiple simultaneous allied drivers remain future work.

Compilation and local native fixtures pass. Standard REV-8 driver pose, front passenger animations, 2 + 2 distribution, exits and reload behavior await the grouped v47 in-game test. Matching roles do not guarantee compatibility with custom meshes/animation sets. The author guide documents indices 0/1/2/3/4 and actual keyword Forms; arbitrary additional indices are not implemented.

### v48 visible settings audit and menu cleanup

All eight visible Gameplay Settings options are consumed by v47: automatic extra rear boarding, deployment limit, automatic convoy, passenger layout, synchronized lights, gradual acceleration, acceleration time and throttle release. Staggered disembarking (local 802) is already excluded from the settings group's visible GOGL list and is not consumed by the current native exit timing. Its hidden record/property is preserved for saved-state compatibility rather than presented as functional.

v48 surgically edits four GPOF text/label payloads and one MESG. Automatic boarding is labeled Automatic rear-seat boarding with its no-extra-vehicle condition explained. Convoy descriptions reflect persistent orders and front passenger support; Fill additional rear seats becomes Fill additional seats. The deployment-limit description states the single allied driver limit. Select next vehicle model is removed from the controller submenu because v47 deployment now copies the active model on opening; its submenu callback indices are updated together. All record identities, grouping references, defaults, numeric values, VMAD bindings and other records are preserved. Validation confirms precisely five records changed and eight visible options, and the controller compiles. Native v47 is retained; the grouped v47 vehicle test is still required.

### v48 standard REV-8 pair confirmed; v49 driver exit correction

The user visually confirmed the full standard REV-8 pair: Lyria in the lead, Adoring Fan driving the additional REV-8 with correct pose, Sarah in its front passenger slot, correct entry animations and following. Stop convoy released both allies. Native logs record Sarah's natural marker-one entry, seated state throughout, driver player clip selection and valid calibration on the third attempt. The lead and extra reference IDs were FF09DFCF and FF09E56C; these are session evidence, not persistent identifiers.

The remaining visual fault is specific to the driver: he appears outside abruptly rather than animating, sometimes colliding violently with the rover. Sarah exits with animation. The cancellation path used exitOriginal at RVA 019942C0 with all three Boolean arguments true, while managed passenger exit uses the ordinary get-up path. Controller EndConvoy also restored iIsPlayer before releasing the order. These are concrete implementation differences; they do not prove which alone caused the fault.

Native v49 replaces immediate forced driver exit with the verified getUpOriginal call for the matching current vehicle/marker-zero occupant. Physical input is stopped first. A scoped driver animation context is supplied only for the synchronous normal get-up call and restored in finally; completed reference ownership is also released in finally. No immediate forced fallback is issued if get-up rejects. Controller v49 preserves its owned iIsPlayer value through the exit and restores it when the loaded actor reaches standing state (or dies), using the existing automation timer. No actor identity or graph ownership changes are made. Visual exit and walking cleanup await the v49 test; accepted get-up alone is not proof of animation.

### Release scope clarified: mounted weapon and vertical recovery first

The user explicitly deferred personal passenger weapons to a later release, potentially 2.0. Enemy vehicles, prescribed NPC A-to-B routes and allied POI travel remain future features. The first release is centered on the allied driver's vehicle; mounted weapon and vertical boost remain its missing major capabilities, alongside exit/lifecycle fixes. The current scope is maintained in docs/VEHICLE-ROADMAP.md; historical backlog entries are now labeled as historical rather than current status.

Additional offline inspection confirms the input routine reads vertical request byte +1E, compares state bytes component +240/+241 and charge float +238 against the engine's minimum charge setting, then writes the native vertical-request state +241. This is an observed engine path, not authorization to write charge or those state fields directly. The mod currently leaves the request byte zero.

Mounted firing byte +1F transitions through latch +578 into 0131D120 and native trigger release. The orientation routine 0131F6C0 reads a shared provider at global RVA 061DD460 and a retained pointer at its +8; later it decomposes a transform at that object's +80 via 02BCDF60 and derives rotation matrices with a weapon-data-dependent pitch clamp. Provider identity and applicability to autonomous aiming are unproven; do not label it a verified camera API or redirect that shared pointer. Full listings are preserved for further tracing. No firing or vertical jump has yet been added to the installed payload builder.

### v49 animated exit confirmed and Deimog report

The user visually confirmed the driver now exits with animation. Native v49 logged four accepted ordinary get-up requests. In the 01:38:25 stop, the controller retained its owned driver pose through exit and restored iIsPlayer to 0 at 01:38:28. Sarah also left the additional vehicle and her order cleared. The user additionally reports successful use of Deimog, with poor navigation in low gravity. This is user-reported model compatibility, not an exhaustive seat/animation matrix. No separate confirmation of every possible collision or walking-state scenario was supplied. Low-gravity steering/airborne behavior is now on the first-release tuning list.

### v50 explicit native vehicle action check

Two hidden diagnostic keywords use local IDs 81B/81C and distinct editor IDs. Only the matching managed convoy driver's owned live record can queue a request. The normal input bridge now has an opt-in diagnostic mode: mode 1 sets only firing byte +1F; mode 2 sets only vertical boost byte +1E. Existing input remains unchanged outside the explicit test. Diagnostics refuse concurrent forward/reverse/steering/forward-boost input and keep the original native call and resource checks. No energy, firing state, camera provider or weapon definition is written directly.

The request planner requires the player to be on foot, a valid finite vehicle pose and 400 ms of observed near-stationary position/height/yaw. Requests expire after 3.5 seconds. Firing requests last nominally 200 ms and vertical requests 150 ms, quantized by the existing native tick. They release earlier after more than one horizontal unit or 0.6 height units. These motion observations do not establish grounded state or overhead clearance. This is an explicitly requested open-area test, not autonomous obstacle recovery. Native application logs the trigger latch, vertical/forward states and charge readbacks within the existing component lifetime; those are execution evidence rather than projectile/damage proof.

Global quoted command RV8VehicleFleet.VehicleActionProbe requests mounted then vertical actions with a notification at completion. The controller requires a loaded seated convoy driver, player on foot and no combat. New keyword records preserve existing forms and settings; no diagnostic menu buttons were added. Local fixtures verify both action bytes reach the native callback, neutral clears them, input scopes/references/gate restore, simultaneous controls reject, stationarity/duration/displacement/height/expiry conditions and malformed pose rejection. Actual firing, vertical motion, damage attribution and independent aiming remain pending the v50 in-game test.

### v50 mounted and vertical actions visually confirmed

The user confirmed the allied vehicle fired its mounted weapon forward and then used vertical boost. Native logs show firing latch +578=1 during the mounted request, returning to zero before vertical request. Vertical state +241=1 was recorded, charge changed from 1.0000 to 0.9506, and height rose from -162.0402 to -161.7948 during the short pulse before returning close to its initial height. These establish both native action paths on the additional vehicle. They do not establish projectile damage attribution, autonomous aiming, enemy selection or obstacle-aware jump policy. Next integration priorities are bounded automatic recovery and mounted engagement of a validated forward target, retaining independent options and cancellation/neutral release.

### v51 automatic action integration (pending game validation)

Both new Gameplay Settings options default to disabled. Automatic vertical recovery requires exhausted forward/reverse attempts, near-stationary observations and a fresh controller policy lease; the native pulse is bounded to 250 ms and displacement/height limits. One pulse is permitted per recovery area. Interior cells and driver combat block the policy. There is no verified grounded-state or overhead-clearance check.

Forward mounted support issues bounded 200 ms requests with at least 1.5 seconds between requests. Controller checks loaded live hostile combat targets, range 10–70 game units, heading within 4 degrees, height within 1.5 units and driver detection LOS. Known companions, player and lead vehicle ahead block requests. Detection LOS is not a muzzle raycast; this is forward firing, not independent turret aiming. It also operates while the player is on foot and the convoy remains assigned. Native motion/heading limits can cancel an accepted pulse. Owned keywords and hit registrations are released on disable/cleanup.

Verified Papyrus single-shot OnHit registrations and Game.GetHealthAV provide aggressor/source/projectile and health-change logging. Health changes alone do not isolate the damage source. Native action readbacks are execution evidence rather than impact proof. Native fixtures and both Papyrus compilations passed; plugin validation preserves prior records except its header and settings group, adding four records. In-game automatic behavior and attribution remain pending.

Packaging audit: legacy seat-keyword plugin includes multi-seat test configuration furniture overrides and declares multi-seat test configuration as a master. Generic runtime model checks do not remove that package dependency. Split generic keywords and optional overrides before standalone release, preserving IDs/references; front-only controller paths also need their rear-keyword dependency removed.

### v52 save/load occupant recovery (pending visual validation)

The reported stuck-occupant save loaded a seated driver/passenger in v51. The quest initialized by restoring pose and ending the convoy before the native process-local registry had been reconstructed. Logs then repeated passenger sitState=3 with release=true. Papyrus references/order arrays persist in the save; native retained occupant records do not survive restarting the game.

v52 defers load cleanup and automation until a bounded loaded-reference recovery pass. SnapIntoInteraction intercepts an already seated, role-keyword-owned occupant and registers its current marker without replaying entry or changing seats. It validates the live furniture handle, actor identity and supported role; duplicate registration is idempotent. Saved convoy sampling resumes and explicit removal can again reach the ordinary animated exit path. A recorded pending passenger release can temporarily restore its recorded tag for rebind and remove it again. Driver recovery from an earlier cleared registry is limited to its still-owned pose actor on a registered additional vehicle. This does not establish recovery for arbitrary untracked occupants or same-process loads retaining stale native records. Local fixtures validate driver/passenger rebind, rejection of player/wrong handle/wrong marker, idempotency and retained-reference balance.

### v52 gameplay result and mounted targeting blockers

User observed occasional automatic vertical boost while recovering, but no mounted firing even after attempting frontal placement. Preserved logs are outputs/RV8VehicleFleet-v52-prueba.log and outputs/RV8NativeTrace-v52-prueba.log. Native recorded four automatic vertical recovery starts and mode-4 requests with vertical state set and charge consumption. This establishes automatic triggering, not success against every obstacle or verified overhead clearance.

There were zero controller mounted-fire requests. Thirteen sampled geometry/LOS blocks show target offsets outside the current 4-degree cone; several have detectionLOS=true, so invisibility alone does not explain failure. The smallest sampled absolute heading is about 11.38 degrees. Other samples show false driver detection LOS, no live combat target or no active combat state. Sampled logging cannot establish every intervening frame. The cone is a controller firing eligibility restriction, not a change to the vehicle turret's movement range. Broadening it without target-specific aim would permit fire in the wrong direction.

Further offline aim analysis: caller 0131D5F0 conditionally invokes orientation routine 0131F6C0 at 0131D828, gated by component +579 and a runtime comparison using +18. The routine decomposes shared-source transform +80 at 0131FADF, uses one output for horizontal rotation and another for weapon-data-limited vertical rotation, then decomposes the vehicle-context scene transform at 0131FC75. It writes local basis vectors to selected scene nodes +40/+50/+60 and invokes node/parent update virtuals +248/+2E8. This identifies a narrower observation point for separating aim from driving. It does not establish a callable target-aim API; no shared provider replacement, scene-node writes or guessed calls were installed. Details preserved in work/mounted-aim-v53-research.json.

Next implementation must identify a viable hostile target across the player's and seated driver's detection contexts, preserve target identity while it remains valid, and verify independent weapon orientation before removing forward-only firing eligibility. Keep vehicle weapon pitch limits and use a muzzle-oriented visibility check when its native contract is established. Player detection LOS alone is not evidence of an unobstructed muzzle. Save-load exit recovery still needs separate visual confirmation; a recovery log with no registered occupants is insufficient.

### v53 reconstruct missing quest orders from actual seat occupants

User confirmed saved occupants remained stuck in v52. Its first load log reported recovery completed on attempt one, with neither driver nor passenger rebind entries. This exposes the missing case: recovery solely from serialized quest orders cannot repair orders already cleared by an earlier load.

v53 scans bounded furniture markers on the quest's registered additional vehicle references using CPE GetFurnitureMarkerUser, validates current furniture and exact supported role keywords, and reconstructs driver/passenger order bookkeeping before the v52 native rebind. It excludes the player, dead occupants, unregistered vehicles and unsupported marker roles. A missing order can now be recovered without requiring its original array entry or driver pose ownership. Explicit Stop runs this discovery as well; a stop requested during unloaded-reference retries is queued and executes after the bounded recovery pass rather than allowing automation to resume. All marker/user/keyword findings are logged for the next test. Papyrus compiled successfully. Native v52 remains unchanged, including its tested idempotent rebind and animated exit path. Actual recovery remains unconfirmed until the saved-game test.

### v54 controller consolidation and dismount preference

Working user-facing name: Enhanced Vehicles. Existing internal filenames, quest IDs and script names are retained. Main controller now offers Deploy vehicle, Convoy controls and Vehicle utilities; individual selection/boarding/recall/status actions moved to utilities. Convoy controls includes a session-persistent automatic-start pause separate from explicit Stop and the Gameplay Settings preference. It does not cancel an existing order.

New local option 821, Exit vehicles with player, defaults enabled. A player vehicle-to-standing transition releases managed lead passengers and ends an active convoy without cancelling its saved order; remount can resume it after occupants stand. Disabled preserves the previous seated waiting behavior. Automatic passenger boarding now gates additional passenger allocation as well as lead rear seats; front companion boarding remains native. After extra allocation, free followers can fill compatible lead rear seats. Automatic convoy default is enabled for new configurations; saved GameplayOption choices remain authoritative. Automatic starts can occur while already driving if an additional vehicle becomes available.

Both scripts compile. Binary validation confirms menu option counts 4/9/7 including Close/Back, existing IDs preserved, one new gameplay option, its visible-group link and defaults. Native v52 and v53 seat-discovery recovery remain intact; in-game lifecycle and menu behavior await grouped validation.

### v54 loaded occupied-save regression (14 October 2026)

User reports both occupants still cannot exit after loading the occupied save, and the saved driver has the front-passenger pose. The same system generally works from another save with followers outside the vehicle. Headlights that were on at save remain on after reload; that observation alone does not establish whether this is stale mod state or normal persisted engine effect state. Do not declare the affected save irreparable or general save/load support complete.

Archived Papyrus sessions: outputs/RV8VehicleFleet-v54-session0.log, session1.log and session2.log. Session2 (21:53) discovers actual REV-8 occupants at markers 0 and 1 on FF0966AB, reconstructs both quest orders, and receives true from both SnapIntoInteraction recovery calls. Explicit stop at 21:53:59 clears the driver session, but the passenger remains sitState=3/release=true until log close at 21:56:18. Thus actual-occupant discovery now works for this session; returning true from the Papyrus interaction call is not proof of native registry reconstruction or successful get-up. Session0 and session1 contain no scanned occupants and cannot replace evidence from session2. Native trace for the problematic session was not preserved by the later launches, so no matching LOAD_REBIND/exit trace is currently available.

Next recovery work must verify native registration independently of SnapIntoInteraction's reported success, account for the already-seated short-circuit path, restore the driver's animation graph on loaded seating rather than only fresh entry, and log the initial actual lights/effect state before enforcing any load policy. Keep ordinary entry and driving paths stable. No additional implementation was installed for this report; v54 remains the current package.

### v55 native saved-seat short-circuit correction

Offline disassembly of registered Papyrus SnapIntoInteraction callback 01FCA990 establishes its final call to 01799E50(actor, furniture). That routine resolves the actor's current furniture handle; at 01799EFE–01799F08 an already matching target sets the return true and jumps to final cleanup without invoking 018F69A0/018F6AD0. This explains why the previous outer-wrapper rebind fixtures passed but saved-game calls never registered actual native occupants. Listings are work/disasm-01FCA990.txt and work/disasm-01799E50.txt.

v55 intercepts 01799E50 before this early return. It validates an existing non-player Actor, current marker, live target handle, supported role and owned role keyword, rebinds that seated occupant and calls the original once. For matching saved-seat scope, true now also requires successful native registration. Explicit acknowledgement logs actor, vehicle, marker and registered result. Outside scope original result/arguments are preserved. Signature mismatch rejects load before installing hooks. Existing outer rebind remains idempotent. Fixtures verify the early-return wrapper actually creates a passenger record, repeated calls do not duplicate it, original executes once and unrelated true/false results are preserved; the full native suite passes.

Only native bridge changed; controller v54 and preferences remain. Loaded driver graph pose is not independently repaired by a guessed graph reset. First validate release; then a fresh ordinary boarding can test pose recovery via the already validated entry path. Saved light effects are unchanged by this correction. In-game recovery still pending.

### Conversation while driving — request and initial inspection (15 October 2026)

User reports dialogue response selection stops vehicle control, including an unspecified vanilla mission. They want to continue actively driving while talking/selecting responses. Existing mods are Shades Vehicle Tuner (11058) and Immersive Driving (11253), both identified locally through MO2 metadata. The latter's installed settings enable Companion Chatter and third-person chatter; the live description documents dialogue initiation, not simultaneous driving/response controls. Shades documents vehicle tuning/storage and states advanced controls require extender work. Do not infer broad compatibility from absence of reported conflicts.

Local vanilla Game.psc exposes IsPlayerInDialogue, RequestDialogueSelect/Skip/Exit and dialogue-camera start/stop functions. These can observe/control a conversation but do not establish a vehicle input unlock. InputEnableLayer.psc explicitly states all layers combine to determine the final controls state. Enabling movement on a newly created layer therefore does not establish that a different dialogue layer's restriction is removed. Blindly enabling/deleting unrelated layers would affect quests and other mods and is not a valid implementation.

Next investigation: distinguish input lost at menu routing from a movement-disable layer, camera takeover and any vehicle-specific control suppression. Observe mapped vehicle input immediately before/during/after dialogue and correlate with native dialogue state; retain the game's bindings rather than hardcoded WASD or fixed throttle. Keyboard/gamepad response navigation must not also steer or brake accidentally. Any eventual override must be scoped to the player driving a land vehicle during ordinary companion dialogue, preserve scripted scenes/quest restrictions and restore its own changes on exit/load/menu transitions. This has not been implemented or installed; v55 recovery remains the current test version.

References: https://www.nexusmods.com/starfield/mods/11058 and https://www.nexusmods.com/starfield/mods/11253 . No code or assets from these mods are copied.
