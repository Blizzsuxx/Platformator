# Final Constitution Review

## I. Performance and scalability

The runtime change keeps the current ordered sprite traversal at O(N), prepares the camera/output mapping once per rendered frame, and submits O(V) visible sprites. The geometry classifier uses stack values and a no-allocation helper; retained runtime state is O(1), with no visibility list, cache, or membership index. Resource probes found zero warmed C++ allocations in stable cases and no mode-specific allocation in motion/churn/headless comparisons. Sparse timing reductions are about 73% at 100 sprites, 80% at 1,000, and 84% at 10,000, beyond observed variation. The bounded matrix includes one- and six-worker profiles. The local `EFFICIENCY:` comment at the traversal records its repeated O(N) cost, why it remains authoritative, and a concrete generation-safe ordered-cache follow-up trigger.

## II. Multicore design with oneTBB

Window submission remains sequential: SDL renderer state is frame-local and sprite compositing order is observable. Culling reads the current scene after existing simulation/structural reconciliation and introduces no competing worker pool. Existing physics/simulation oneTBB work is untouched. Selected six-worker rendered fixture cases and headless crossing were captured, and existing headless engine controls ran at one and six workers. Rendered stock-engine controls remain one-worker integration checks; no culling speedup from multicore execution is claimed.

## III. Research-backed collision architecture

No broad-phase, collision, physics, or solver implementation changed. The culling bounds are renderer-output bounds and are not reused for collider or contact decisions. No collision paper-derived invariants were altered.

## IV. Simple code and KISS

The public configuration is one appended boolean. The implementation uses one private per-frame value view, one internal geometry helper, the existing ordered vector, and the existing camera drawing path. Direct public `Camera::render` stays submit-all. Custom SDL mapping falls back for one frame without changing SDL state. No dirty-transform notifications, persistent cache, backend enum, worker pool, or new dependency was introduced.

## V. Unity-inspired developer experience

Window culling is default-enabled and requires no per-sprite authoring. `--no-render-culling` is available for the runtime, stock benchmark runner, and focused probe. The setting is not serialized into scenes and has no runtime mutation API. C++ consumers must rebuild for the appended header field; the package consumer compiles and runs with it. No new Unity-specific behavior claim is made.

## VI. Evidence before performance claims

The pinned submit-all source and common harness, per-input source hashes, exact binary identities, separate correctness/resource/timing artifacts, actual focused-probe renderer mapping, and stock-runner control limitations are documented in the persistent T044 bundle. Sparse gains are large across five independent processes. All-visible mean estimates are positive, but their Welch 95% intervals include zero; this is treated as potential overhead with uncertain magnitude. Overlapping min/max ranges are not evidence of no regression. Headless behavior, engine controls, and tails are reported separately. SC-005's sparse benefit is supported only for the tested SDL software renderer and bounded workload matrix; there is no hardware/GPU claim.

## VII. Correctness and architectural integrity

The independent double-precision geometry oracle, static image/order tests, current-value dynamic/camera/lifecycle checks, custom mapping fallback tests, direct Camera compatibility, headless work counters, and ASan/UBSan integration passed. The view retains no object references; the fixture reacquires borrowed textures through Runtime before Sprite assignment. Observation callbacks are work-probe-only and detach before runtime teardown. There are no changes to physics scheduling, lifecycle ownership, scene serialization, or third-party dependencies.

## Artifact consistency and signoff

The T044 baseline/candidate fingerprints are `536cd40de0dea634250c56ffca20e68231ded659cef7257c9b8b9d60ee42529f` and `4a5ce52e10fc81f903c3687879ee9d883836ec13668e18185f967fc6fa20dfba`. Shared fixture/harness hashes are `caff033f8a7577eb7d3f9eafe5a897bb09a70db78b385a7d010d4c174d84e65d` and `83f7119f120a7a3fd401028553ee29595b23e93a87f101b58ecb3611bc1abbd7`. Separate timing/validation builds, source manifests, binary hashes, common baseline patch, proofs, and raw runs live under `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`. Strict validation accepted 51 rendering proofs, 51 resource proofs, 255 focused timing processes, and 60 engine controls. The previous raw capture roots were unavailable; their summaries remain as superseded snapshots, not current measurements. See [final-validation.md](final-validation.md) and [capture-index.md](capture-index.md).

The implementation review is signed off against the seven principles. T001–T044 and SC-001–SC-005 are complete within the documented software-renderer and workload boundary. The final evidence record is in [RESULTS.md](RESULTS.md) and [capture-index.md](capture-index.md).
