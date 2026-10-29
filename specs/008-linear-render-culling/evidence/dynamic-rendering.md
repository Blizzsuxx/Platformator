# Dynamic Rendering Evidence

The scenario-specific integration and sanitizer details below are the pre-T044 implementation-validation summary; their original external raw logs were not recovered. T044 made no engine changes and its candidate CTest passed 34/34, including current geometry/rendering integration. The newly retained 600-step focused proof matrix is described in [matrix-validation.md](matrix-validation.md). T044 did not rerun sanitizer builds.

## Trajectories and rendered dynamics

`tests/render_culling_fixture.h` now supplies deterministic `crossing`, `camera-pan`, `churn`, `sparse`, `clustered`, and `rotated` profiles. Positions depend on seed, logical slot, and fixed step. Crossing and pan use a 240-step period; churn keeps its population fixed, replaces at least one slot per step (1% for the tested population), and increments logical generations.

Trajectory arithmetic checks ran 720 steps for 40 crossing sprites and recorded 200 horizontal and 200 vertical entry/exit events. The camera-pan arithmetic check ran 720 steps for 80 stationary sprites and recorded 54 entries and 53 exits. The churn arithmetic check ran 600 steps for 1,000 slots and observed 6,000 replacements. Those counts describe trajectory contracts; they are separate from render captures.

The integration test now renders matching submit-all and enabled sequences through the shared fixture and real SDL renderer. Each mode renders 120 warmup frames and 720 measured frames for 40 continuously moving sprites. Every measured frame compares output dimensions, a normalized pre-present image hash, and the enabled identity trace against the reference trace filtered by the independent visibility oracle. The run observed 223 oracle-visible entries and 233 exits across measured frames. The sequence also changes parent and sprite transforms, sprite dimensions/source/texture, camera dimensions and camera component, activation, removal, replacement, and window output size after an earlier rendered frame. Readback buffers and oracle dimensions follow SDL's actual output size; the resized output is 400×300. It removes the camera, freezes the remaining scene state, and verifies repeated unchanged image hashes and traces. This regression is distinct from the focused-probe crossing and camera-pan proof matrix.

## Same-frame lifecycle and drawing inputs

Focused enabled/reference scenarios passed for parent movement; size, scale, rotation, source rectangle, flip and texture changes; activation; insertion; swap-removal; component replacement; destruction; camera component replacement and dimension changes; and scene clear/load followed by rebuild. Traces are compared in the current authoritative submission order after filtering the submit-all trace through the independent visibility oracle. Replaced slots use incremented generations. The final cases produced these reference/enabled submitted counts and image hashes:

| Scenario | Reference | Enabled | RGBA FNV-1a |
|---|---:|---:|---|
| Parent movement and drawing inputs | 4 | 3 | `aa2b3da2aff4bc23` |
| Activation/insertion/removal/replacement/destruction | 3 | 3 | `a11c1bb7337816cd` |
| Camera replacement/dimensions | 4 | 3 | `db6cfce071613b85` |
| Scene clear/load/rebuild | 2 | 1 | `69f054e50f2a7a15` |

## SDL mapping fallback and API compatibility

Work-probe checks changed viewport, render scale, logical presentation, and render target before the render traversal. Each change caused one eligibility check and zero sprite visibility checks; all three offscreen sprites were submitted. The next frame restored the default mapping and culled all three. A direct call to the unchanged four-argument `Camera::render` submitted an offscreen sprite, preserving its submit-all behavior.

These checks ran in the SDL software renderer with a 320×240 output. The work-probe build passed the targeted CTest set (runtime recreation, transform hierarchy, runtime step, runtime work, geometry, and rendering integration). The ordinary debug-tools-on build passed the same applicable targets; its image/trace integration test returned the explicit skip code because probes are compiled out.

The targeted ASan/UBSan build with work probes initially caught the fixture's stale borrowed texture-cache pointer during scene reload. The fixture now reacquires wrappers through the Runtime immediately before assigning them to a Sprite; it does not retain wrappers past Sprite ownership. Dedicated paired render regressions cover single-user idle replacement, removing the last user and rendering an empty frame before re-adding, switching away from and back to a texture, and scene reload. `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir bin/cleanup-asan -R render_culling --output-on-failure` passed both geometry and rendering integration tests after the fix. LeakSanitizer itself fails in this environment with `LeakSanitizer does not work under ptrace`, so leak detection is not claimed.

These checks establish software-backend rendering/mapping behavior; hardware renderer behavior remains unverified on this host.
