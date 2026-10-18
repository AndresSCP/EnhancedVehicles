# v62 controller correction

Start convoy on foot now reserves the chosen driver and extra vehicle without issuing a boarding request. Only a valid player-driven lead allows the retained order to begin. BeginConvoy independently enforces that condition, so callers cannot bypass the timer gate.

With Exit with player enabled, leaving the lead releases allies and retains the convoy order. It resumes only after the player returns to driving. With that setting disabled, the seated driver remains waiting as before. Stop convoy cancels the retained order. This fixes the v61 loop in which EndConvoy(false) was followed by an on-foot BeginConvoy and repeated dismount/remount.

Papyrus compilation passed. No native, ESM, settings default, private patch or profile changes are required. The v61 flight test remains available: for a parked test, disable Exit with player, enter the lead to start normal boarding, then dismount while the ally remains seated. Regular Start is no longer a shortcut to park the allied driver while the player stays on foot.

Flight evidence from the preceding v61 session: the request ran from 17:25:41 to 17:26:01, issued 18 pulses with sampled burn 3938 ms, released for descent and expired with reason 2. Logged relative heights repeatedly rose by fractions of a unit then returned near origin. This establishes vertical response, not sustained hover. Short pulse duration is a plausible tuning issue; vehicle model, available resource and gravity must also be considered. No improved flight control is claimed by this controller-only fix.
