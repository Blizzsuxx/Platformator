# User Story 1 acceptance (T032)

## Default and selected-backend tests

The probes-enabled `bin/cleanup-work` build completed. The full non-package CTest run passed 36
tests; `platformator_benchmark_contract_test` was skipped because the work build has benchmark
capture disabled. The separate `platformator_package_smoke` test passed, including source,
installed, relocated and Mario consumers. Together, the applicable default physics, contact,
scene/lifecycle, tree, spatial selection, broad-phase and integration tests passed.

Commands:

```sh
cmake --build bin/cleanup-work --parallel 4
ctest --test-dir bin/cleanup-work -E '^platformator_package_smoke$' --output-on-failure
ctest --test-dir bin/cleanup-work -R '^platformator_package_smoke$' -V
```

The runner was also exercised headlessly with culling `none` for each broad phase, using the same
seed, population, step counts, worker setting and validation mode:

```sh
bin/cleanup-benchmark/platformator_benchmark_runner --scenario broad_phase --broad-count 32 \
  --warmup-frames 1 --measure-frames 5 --workers 1 --seed 0 --broad-phase segmented \
  --render-culling none --validate-invariants --csv-output /tmp/us1-segmented-smoke.csv
bin/cleanup-benchmark/platformator_benchmark_runner --scenario broad_phase --broad-count 32 \
  --warmup-frames 1 --measure-frames 5 --workers 1 --seed 0 --broad-phase bvh \
  --render-culling none --bvh-maintenance native-partial --bvh-padding 0 \
  --bvh-query-execution serial --validate-invariants --csv-output /tmp/us1-bvh-smoke.csv
```

Both schema-3 manifests report validation-only, timing-ineligible captures and no rendered frames.
Each CSV retains five samples per complete-step, physics, cleanup, broad-phase, narrow-phase and
collision-resolution scope. Complete-step averages in these single-process smoke captures were
0.0169502 ms for segmented and 0.0122792 ms for BVH; these short validation samples establish the
configured execution and metric boundaries only. They are not five-process comparisons and support
no speed claim.

Artifacts and SHA-256 checksums:

| Artifact | SHA-256 |
|---|---|
| [Segmented CSV](us1-smoke-captures/us1-segmented-smoke.csv) | `3ac7d2e17ff43b54166bf8b4c8ecaec68651a796eddcd8f3dd0f4b2c4f599eb6` |
| [Segmented manifest](us1-smoke-captures/us1-segmented-smoke.manifest.json) | `8132a09000e7bb37d7f9b3de5859cc7a38f17746530d12d90ae232ee69fb2972` |
| [BVH CSV](us1-smoke-captures/us1-bvh-smoke.csv) | `237499a5121f93f90ff457289e832b709d098c61799fc896fd0e850230fae60c` |
| [BVH manifest](us1-smoke-captures/us1-bvh-smoke.manifest.json) | `fe55ed29c07766a91aa9ea4fef445d53ae738071f405e8b0f5f4dd0068542c43` |

## ASan/UBSan and TSan

The `cleanup-asan` address/undefined-behavior configuration passed all 27 applicable
physics/lifecycle/spatial tests with `ASAN_OPTIONS=detect_leaks=0`; the three work-probe tests were
skipped because probes are disabled in that preset. With leak detection enabled, LeakSanitizer
terminated each test with its ptrace fatal error, so leak checking is unavailable in this runtime.

The `cleanup-tsan` configuration passed the seven tree and spatial tests:
`dynamic_aabb_tree`, `spatial_pair_state`, `spatial_segmented`, `collision_broad_phase`,
`spatial_physics_integration`, `spatial_selection`, and the vendored Box2D tree suite. A broader
physics/lifecycle run emitted an SDL3_mixer startup race report at
`SDL_PlaybackAudioThreadIterate` while `SDL_SetAudioStreamFormat` initializes the dummy audio
stream; nine tests containing runtime audio initialization were reported failed by TSan. The new
spatial tests themselves passed without a TSan report. This environment limitation is retained
rather than suppressing the audio diagnostic.

These gates establish correctness under the available instrumented configurations, with the
LeakSanitizer and unrelated SDL3_mixer limitations above. Rendering and all eight cross-product
selections are validated in later user-story tasks.
