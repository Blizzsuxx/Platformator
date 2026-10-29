# Shared Fixture and Observation Support

## Implemented support

- `tests/render_culling_fixture.h` creates a fixed output and camera through the public `Runtime` API, loads real repository textures, and creates collider-free sprites with explicit logical `{slot, generation}` identities. It is baseline-compatible: it does not reference the planned culling setting.
- `tests/runtime_work_probe.h` provides render view/eligibility/visibility/submission/readback counters, a scoped observation attachment, an ordered identity trace into caller-owned reserved vector capacity, and a pre-presentation callback. The callback only receives the renderer for output inspection and the dimensions; it has no runtime mutation API.
- `src/workprobe.h`, `src/camera.cpp`, and `src/sdlwindow.cpp` connect actual draw submissions and the pre-present point to the probe. The existing `SDL_RenderTextureRotated` arguments/order and `SDL_RenderPresent` order remain unchanged.

## Build checks

- `cmake --preset cleanup-debug && cmake --build --preset cleanup-debug --parallel 4`: passed on the ordinary configuration.
- `cmake --preset cleanup-work && cmake --build --preset cleanup-work --parallel 4`: passed with `PLATFORMATOR_ENABLE_WORK_PROBES=ON`.
- A syntax-only translation unit including `render_culling_fixture.h` compiled with both `PLATFORMATOR_ENABLE_WORK_PROBES=0` and `=1`.
- `nm -C bin/cleanup-debug/libplatformator.a`: no `observeSpriteSubmission`, `observeBeforePresent`, or `activeRenderObservation` symbols.
- `nm -C bin/cleanup-work/libplatformator.a`: the expected observer functions and active attachment are present.
- Frame readback, submission traces, correctness proofs, and performance captures were completed in later tasks; see [static-rendering.md](static-rendering.md), [matrix-validation.md](matrix-validation.md), and [RESULTS.md](RESULTS.md). This foundation note records the support stage only.

## Common support to apply to the pinned baseline

The baseline-compatible support consists of the fixture header and work-probe additions in `tests/runtime_work_probe.h`, `src/workprobe.h`, `src/camera.cpp`, and `src/sdlwindow.cpp`, plus the required CMake target registration added with the focused tests. It adds no camera preparation, culling setting, renderer-state eligibility checks, or filtering. The exact patch and its hash will be recorded after the focused test/probe target list is finalized in T026.
