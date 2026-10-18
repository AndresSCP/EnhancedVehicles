# v59 native correction

The v58 game test confirmed manual convoy start while the player is on foot. Its strict measured-trail navigation performed worse in actual terrain and is withdrawn from active navigation. v59 restores v56 direct pursuit, its elevation support, steering stabilization, recovery and boost policy. It retains v58 explicit passenger release and calibration waiting. No controller/plugin/profile change is required.

The trail implementation is retained only as inactive experimental code/local fixtures; runtime route recording and route-target selection are disabled. Passing its mathematical fixtures did not establish usable vehicle navigation on terrain. This is not collision-aware pathfinding.

Passenger evidence: natural marker-3 entry was accepted at 23:31:33, with controller reports of SitState 3 and release=false through 23:31:50. At 23:31:51.669 the native Exit hook was called directly with flags 111; its stack includes 00DF8BDF/00DFEDAD, bypassing the queued GetUp exit path. The actor's furniture was already cleared before the controller removed its keyword at 23:31:52. The controller cleanup therefore did not initiate this first exit. A later boarding retry returned false. Cause remains unresolved. Do not claim v59 fixes unwanted passenger exit.

v59 adds a diagnostic at this existing direct Exit hook for tracked occupants, recording exact seat/handle, role presence, order state and arguments while always preserving the original call. It does not block potentially necessary forced exits.

Local native build and regression fixtures passed. In-game navigation comparison and the unwanted-exit cause remain pending. The separate private compatibility patch continues to supply extra-seat keyword references.
