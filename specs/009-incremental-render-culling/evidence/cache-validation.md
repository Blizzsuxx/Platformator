# Cached Linear Validation

## Test first: initial failing result

`tests/render_culling_test.cpp` now contains cached-linear regressions for empty, visible, sparse, and all-offscreen layouts at several populations, corresponding current-linear/submit-all frame comparisons, unchanged-frame work reuse, same-value setters, repeated changed-back edits across multiple simulation-only steps, and parent-subtree updates that must leave unrelated records untouched.

Configured the isolated Release/work-probe build at `build/009-incremental-validation`, built `platformator_render_culling_test`, and ran:

```sh
ctest --test-dir build/009-incremental-validation --output-on-failure -R '^platformator_render_culling_test$'
```

The test compiled and failed on its first cache assertion: the unchanged empty scene reported zero cache hits. This is the expected pre-implementation failure; the current renderer still walks/handles every frame without an ordered-result cache. The remaining cases are present but stop after this first failure. They will be rerun after the cache and registration paths are implemented. No implementation acceptance is claimed from this run.

## Implemented cache result

The candidate now owns one dense handle record per registered Sprite in cached modes. Transform synchronization, scale/rotation setters, sprite dimensions, texture eligibility, and owner-thread physics rotation publish deduplicated dirty ranks. Add/remove and swap relocation preserve the window's authoritative Sprite order. Cached-linear refreshes dirty bounds once, rebuilds ordered visibility only when the camera/output view changes, and redraws selected sprites from their live texture/source/flip values on every frame.

The static-layout assertions passed for empty, visible populations of 1/32/256, sparse-128, and all-offscreen-128. On each unchanged frame they observed zero bounds refreshes, grid queries, full-population visits, and order shifts, with one cache hit. The changed-back setter sequence observed one enqueue and one bounds refresh after five simulation steps; moving a two-Sprite parent subtree observed two enqueues and two refreshes, leaving unrelated records untouched. The cached frame images and ordered submissions matched current-linear and the independent visibility oracle. Submit-all preserved every registered Sprite in order and matched the rendered image.

Inactive-mode contracts passed for current-linear, disabled culling, and headless execution, including zero culling-state creation or dirty publication where state is absent. The no-camera frame cleared and presented, and the public four-argument `Camera::render` still submitted an offscreen Sprite.

Final focused commands:

```sh
cmake --build build/009-incremental-validation --target platformator_render_culling_test platformator_render_culling_contract_test platformator_render_culling_geometry_test platformator_runtime_work_test --parallel 4
ctest --test-dir build/009-incremental-validation --output-on-failure -R '^(platformator_runtime_work_test|platformator_render_culling_geometry_test|platformator_render_culling_test|platformator_render_culling_contract_test|platformator_package_smoke)$'
```

All five selected CTest entries passed, including installed/relocated public consumers and the source consumer. Total CTest time was 334.38 seconds; package smoke took 328.61 seconds. The final run fixed one test-harness comparison issue discovered during validation: submit-all now checks full ordered submission membership directly, while current-linear/cache outputs are compared with the independent-oracle ordered subset. The initial expected T008 pre-implementation failure above remains as the TDD record.

This closes the cached-linear US1 checkpoint only. The maintained spatial grid, full transition/lifetime matrix, and measured performance acceptance remain open.
