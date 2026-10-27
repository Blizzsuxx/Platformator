# Candidate debug and Mario validation in progress

The debug-enabled engine built successfully after removal of the empty DebugObject destructor. The ordinary debug suite excluding package smoke passed, but an equivalent graphical comparison and measured copy/move counts remain outstanding. The Mario example and debug-off build are being checked in the candidate production/package builds; no scene round-trip or visual equivalence is asserted yet.

The production build (debug tools off) passed the applicable suite excluding package smoke, and its rebuilt `mario_example --smoke-steps 5` completed with dummy SDL drivers. The separate debug package/consumer smoke also passed. Existing serialized camera-lead round-trip and real-renderer comparisons have not yet been independently established.

A compile-time check built with each debug tree's own compile command verified that `DebugObject` was not nothrow-move-constructible in the frozen baseline and is nothrow-move-constructible in the candidate. Sources and compiler logs are retained under `/tmp/platformator-006-baseline/*debug-move*`. This does not count actual vector copies/moves or prove visual equivalence.

A matching independent Mario round-trip host loaded the existing level, ran one step and saved it. Both baseline and candidate preserved the serialized `cameraLead: 90`; method and logs are in `us3-mario-baseline.md` and `work/`. Dummy SDL output does not establish visual equivalence.

A graphical attempt with `SDL_VIDEODRIVER=x11` and the candidate production Mario binary failed at backend initialization with `x11 not available` (`/tmp/platformator-006-baseline/visual-attempt.log`); graphical overlay and gameplay equivalence remain unverified. The baseline/candidate debug-growth work fixture found 1,023 replaced/preserved vertex buffers respectively; method is in `work-memory.md`.
