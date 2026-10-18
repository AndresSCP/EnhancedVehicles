# v65 hover-duration experiment

v64 native telemetry: from approximately 19:49:01 to 19:49:07, sampled relative altitude stayed around 2.4–3.0 game units. At sampled burn 3953 ms, the test stopped requesting thrust despite remaining in stage 2, then returned near origin before the planned descent phase. This is evidence of several seconds of altitude regulation, not visual smoothness, guaranteed engine resource availability or autonomous flight following.

v65 raises the sampled thrust-time budget from four to ten seconds while retaining 650 ms maximum holds, 100 ms gaps, predicted-height release, six-unit ceiling, drift/stale/velocity checks, 14-second control cutoff and 20-second expiry. The sampled budget limits controller requests; it is not the vehicle's own boost-energy capacity. The engine's boost resource field is logged but not modified or treated as a verified authoritative fuel value.

A local fixture explicitly requires thrust after the old budget would have been exhausted, while retaining the time cutoff, neutral descent and bounded burn checks. Ground navigation, menu, automatic driver preparation, cleanup and configuration remain unchanged. Next game check: does altitude regulation persist until the descent phase, and does the visual motion appear sufficiently smooth? Horizontal flight control and terrain clearance are not implemented.
