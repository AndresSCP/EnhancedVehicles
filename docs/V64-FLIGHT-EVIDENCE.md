# v64 boost-flight tuning

v63 in-game preparation worked: driver entry at 18:53:14, automatic request at 18:53:17, timed native expiry at 18:53:37 and session release at 18:53:38. The user tested Sub Orbital Shuttle and observed continuous short stationary hops. Logs likewise show fractions-of-a-unit elevations returning near origin, with 18 pulses and sampled budget 3940 ms. This is not sustained-flight success.

v64 changes only the vertical test pulse policy: maximum hold 150 → 650 ms and release gap 250 → 100 ms. Predictive ascent cutoff, relative height envelope, four-second sampled input budget, 20-second expiry, displacement/stale-sample rejection and neutral descent observation are unchanged. Native vehicle resource checks remain authoritative. The controller stays v63, and its self-contained test workflow is unchanged.

Native fixtures include maintaining a single pulse beyond the previous short-hold duration, releasing by the new bound and starting the next pulse after its gap; rapid ascent still releases early. This is a conservative response experiment, not automatic flight following or verified stable hover. If longer holds still produce only short hops, inspect resource behavior and native vertical boost state rather than merely increasing the duration further.

Public documentation uses the test vehicle name only as user-reported context; no third-party assets are bundled. No gameplay settings change is required.
