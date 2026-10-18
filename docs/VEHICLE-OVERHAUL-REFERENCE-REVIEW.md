# Vehicle Mod Overhaul WIP — source review

Reviewed 17 October 2026. Repository: https://github.com/creativityrebranded-cpu/Vehicle-Mod-Overhaul-WIP . Snapshot d3641de021ec6608b7cd7d38a445835b643eca84, commit subject: NPCs force the player out: owners reclaim, hijackers, guards. Downloaded for read-only source inspection; its bundled DLL was not installed, loaded or executed. No third-party source was incorporated into Enhanced Vehicles.

## Implemented approach and stated validation

README describes SeamlessVehicle 0.60.0 for Starfield 1.16.244, SFSE, native hover flight, gears, summon/follow, NPC escort/pursuit and a vehicle registry. It explicitly separates claimed in-game tests from built-but-untested functionality. Multi-follower formation/landing, six NPC vehicles, weapon firing/aiming, registry cleanup and several recovery behaviors are listed as not yet tested. Do not present these as confirmed stable gameplay. Its current registry/selection workflow says it does not spawn vehicles, unlike our explicit deployment controller.

vehicleflight.cpp writes rigid-body linear velocity (lv[0..2], lines 857 onward), manages attitude and integrates a custom lift controller. Its boost-update hook writes the boost component resource field around +0x238 for controlled contexts. Thus its smooth hover is not evidence that equivalent behavior has been achieved through ordinary vertical/horizontal booster inputs. Its direct physics approach is materially different from our native input experiment.

## Ground sensing — high-priority independent verification

addresses.h identifies GroundRayCast RVA 00583B70, TES pointer 05E716F8, and ship-system update 02169100 for this runtime. vehicle_probe.cpp calls the purported ABI `(TES*, const float position[3], uint32 mask, float above, float length) -> float groundHeight` in its hooked update, using mask 0x22006 by default. Requests/results occupy a fixed slot array with age counters.

vehicleflight.cpp AskCorridors/ReadCorridors/Corridor samples ground heights along three headings at three distances, derives clearance/climb demands and steers detours. This is not a demonstrated arbitrary segment collision query. The source itself records a platform missed by the selected layers, which led to diving toward terrain below it. Unknown samples are skipped in Corridor and handled by separate speed policies. Ground below roofs, tunnels, overhangs and different collision layers require independent tests.

Potential value to Enhanced Vehicles: altitude above sampled ground, descent braking policy and terrain-profile anticipation without changing the vehicle's boost resource. Before any runtime integration, independently validate the function prologue, ABI/call sites, world-space units, masks, missing-ground sentinel, execution thread/physics scope and bounded result lifetime against the local executable. Third-party comments are leads, not our verified reverse-engineering evidence.

## Passenger exit — investigation lead

npcvehicles.cpp names a native dismount function at RVA 01952530 and hooks it with `char(actor*)`, denying it for Following jobs unless its own Leave path permits it. This may help investigate our unexpected direct passenger exit but does not establish the same call chain or a safe general hold. Our recorded forced-exit stack and player/vehicle/seat checks must be compared independently. Do not block every forced exit just because the external prototype does so.

## Mounted fire — related path, different aiming

npcvehicles.cpp SubmitFire uses vehicle input submit 012C0550 and apply 01324650, and scopes gate +0x579 around recognized input buffers. This overlaps the mechanism already used by our native bridge. Its combat target is presently the player. AimProjectiles rewrites projectile movement direction, velocity and rotation after firing (offsets +0x158/+0x164); that is not verified turret articulation or normal weapon aiming. README also lists weapon firing/aiming as untested in-game at this snapshot. Do not infer solved ally target acquisition, turret rotation or safe friendly fire behavior.

## Reuse and compatibility

No LICENSE/COPYING/NOTICE file was found in the tracked snapshot. The author's public announcement invites others to build on the project: https://www.reddit.com/r/Starfield/comments/1x8nz2r/vehicle_overhaul_mod_currently_wip/ . That statement is relevant but does not supply a complete standard license or resolve all bundled third-party material. Record attribution/provenance and clarify distribution terms before copying implementation into a release. Independent verification and discussion of the native mechanisms can proceed without installing or copying their DLL.

Both projects hook vehicle input/animation/physics-related routines, including 01324650 and 0131D2C0-related paths. Simultaneous operation is untested; do not claim interoperability merely because filenames differ. No compatibility or API dependency was added by this review.
