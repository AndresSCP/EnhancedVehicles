# Enhanced Vehicles — Starfield WIP

NPC-driven convoys, additional vehicles and passenger-seat management for Starfield PC. This repository publishes the current experimental source and research so other modders can review, reproduce and improve it.

**Development snapshot, not a stable public release.** Current source: controller v63, native v65, consolidated plugin records v61. See [status and known issues](docs/STATUS.md) before testing. There is no prebuilt DLL or compiled Papyrus package in this initial source publication.

## Current scope

- Deploy and recall an additional drivable vehicle based on the active model.
- Reserve a follower as an allied driver; ordinary convoy boarding waits for the player to enter the lead.
- One allied driver with ground pursuit, headlights synchronization, acceleration tuning and bounded recovery attempts.
- Human passengers allocated to compatible marker roles; standard seats and optional extra-seat authoring contract.
- An explicit, self-contained vertical boost experiment. Autonomous airborne following and controlled landing are not implemented.

Gameplay validation varies by feature and vehicle. Mounted triggering has been observed, but autonomous aiming/support fire remains experimental. Occupied-save recovery and unwanted NPC exit are unresolved. Cleanup source has been improved; no leak-free or pointer-handle endurance claim is made.

## Dependencies

Starfield PC **1.16.244.0**, matching **SFSE** (tested 0.2.21), official REV-8 content **SFBGS004.esm**, and **Cassiopeia Papyrus Extender** installed separately. Optional OSF UI/Settings can expose the existing Gameplay Options. Framework/game files are not bundled. See [credits and permissions](docs/CREDITS-AND-PERMISSIONS.md).

## Build and test

Use Windows x64, Visual Studio 2022 C++ tools and CMake 3.24+:

```powershell
cmake -S native -B native/build -A x64
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Compile the two project Papyrus scripts with the installed Creation Kit compiler and external CPE source imports:

```powershell
.\tools\Build-Papyrus.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Starfield' -CpeSourceDirectory 'D:\Mods\Cassiopeia Papyrus Extender\Scripts\Source\User'
```

The example paths must be replaced with your local install paths. Native output is `native/build/bin/Release/EnhancedVehicles.dll`; Papyrus output is `Data/Scripts/*.pex`. Stage the DLL under `Data/SFSE/Plugins/`. To enable the experimental native controls, create the empty marker `Data/SFSE/Plugins/RV8PassengerExperiment.enabled`. Install the resulting Data folder as one mod and activate **EnhancedVehicles.esm**. Never install two native bridge versions together.

`CreationKit/EnhancedVehicles.esp` is the editable alternate of the included ESM. Load only one of these plugin identities at a time. The ESM has only Starfield.esm as a declared master; official seat roles are resolved at runtime. No vehicle-specific overrides or demonstration meshes are included. Existing prototype-save migration is not supported; preserve original saves and test on a separate copy.

## Runtime interface

Vehicle Controller provides Deploy/Recall and Convoy controls. General preferences live in Gameplay Settings. The explicit **Boost flight test** handles its own driver preparation while the player remains on foot; it requires no settings changes. It lasts approximately 20 seconds after seating and is not normal flight-follow mode.

## Research and collaboration

Start with [research and seat authoring](docs/ENHANCED-VEHICLES-RESEARCH.md), [machine-readable seat contract](docs/SEAT-CONTRACT.json), [lifecycle audit](docs/VEHICLE-LIFECYCLE-AUDIT.md) and [contribution guidance](CONTRIBUTING.md). Native addresses are version-specific research leads, not supported game APIs. The strict player-trail experiment was withdrawn from active navigation after poor terrain behavior; its inactive code remains for study.

Project-authored source is MIT licensed. MinHook and the SFSE header retain their included notices. No code or assets from Vehicle Mod Overhaul WIP have been incorporated; [its source review](docs/VEHICLE-OVERHAUL-REFERENCE-REVIEW.md) records research leads only.
