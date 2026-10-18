# Current status — 17 October 2026

| Area | Evidence and limits |
| --- | --- |
| Deployment/recall | Player deployment and driving observed. Retirement calls Disable/Delete; controller v60+ prunes historical naming references. Engine reclamation/handle plateau has not been measured. |
| Passenger roles | Distinct seats and animated entry/exit observed in prior supported layouts. A later extra passenger exited without a controller release; unresolved. No private compatibility patch is included here. |
| Ground convoy | Direct pursuit restored in native v59 after trail navigation regressed. One allied driver; obstacles, low gravity and recovery remain imperfect. |
| Order lifecycle | Controller v62 gates ordinary boarding/resumption on player driving. Controller v63's dedicated flight test boards independently; that preparation was observed working. Full ordinary lifecycle regression still needs checking. |
| Boost flight | v64 telemetry and user report establish several seconds aloft with normal booster inputs. v65 extends the sampled thrust-time budget; in-game outcome is still pending. No horizontal aerial formation, ground clearance sensor or controlled landing. |
| Mounted weapon | Explicit fire input observed. Autonomous target/aiming behavior is not a stable feature. |
| Save/load | Occupied-save release/pose recovery remains unresolved. Fresh sessions working does not establish occupied-save compatibility. |
| Tests | Native fixtures and Papyrus compilation pass in the development workspace. They do not simulate engine physics or replace in-game testing. |

Native v66 adds a longer bounded reverse envelope and stale lead-reference expiry; both pass local fixtures and are not yet game-validated. It retains native v65 boost-flight policy. See OVERHAUL-DEEP-REVIEW.md for external-source review scope and VEHICLE-DISABLE-RESEARCH.md for the future health failure design. No new ground/physics/dismount hooks are enabled.

## Next research priorities

Verify ground-height query ABI/thread/layers against the exact executable; regulate descent; combine vertical control with horizontal following; investigate spontaneous NPC exit; measure pointer-handle behavior across deployment/recall cycles. Dialogue while driving, personal passenger weapons, enemy vehicles and scheduled A-to-B traffic are future work.
