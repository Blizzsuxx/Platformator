# Validation Guide: Render Culling Cleanup and Sprite Z Ordering

Run these checks for the combined culling and ordering implementation. The feature tests, probe workloads, and driver comparison described here are implemented in the repository.

## 1. Record the pre-change baseline

From the repository root, use the current CMake/Ninja, C++20, Python 3, and vendored dependencies. Read `AGENTS.md` and `engine/benchmarks/README.md`.

Set `RENDER_BASELINE_PROBE` to the absolute path of the pre-change timing executable. Before runtime changes, record the current source revision, dirty source fingerprint, and Release timing-probe identity. Reuse a matching build if available. Otherwise build the existing probe once under `build/`. Keep binaries and build trees outside the report directory.

Set `RENDER_BASELINE_PROBE` to the absolute path of the pre-change timing executable (when running endpoint comparisons against the frozen pre-removal binary). Record the current source revision, dirty source fingerprint, and Release timing-probe identity. Reuse a matching build if available, keeping binaries and build trees outside the report directory. Since the render grid is retired in favor of sole production culler `cached-linear`, passing `indexed` is restricted to the frozen pre-removal baseline binary and any such endpoint comparison must be labeled explicitly as combining implementation cleanup with the policy change (`pre-removal-grid-vs-cached-20261118` preserves the historical same-build `indexed` vs `cached-linear` sparse-pan tradeoff). A missing baseline remains unavailable rather than an assumed zero-cost result.

## 2. Focused correctness and work checks

Configure the standard test preset, build the relevant runtime/editor targets, and run the selected tests:

```sh
cmake --preset test
cmake --build --preset test --parallel 4 --target \
  platformator_sprite_ordering_test \
  platformator_render_culling_test platformator_render_culling_state_test \
  platformator_render_culling_geometry_test \
  platformator_render_culling_worker_test platformator_render_culling_trajectory_test \
  platformator_render_culling_contract_test \
  platformator_scene_io_contract_test platformator_runtime_template_test \
  platformator_editor_inspector_interaction_test platformator_editor_preview_test \
  platformator_editor_diagnostics_test platformator_editor_template_test \
  platformator_editor_component_operations_test
ctest --test-dir build/test --output-on-failure \
  -R '^platformator_(sprite_ordering|render_culling|render_culling_state|render_culling_geometry|render_culling_worker|render_culling_trajectory|render_culling_contract|scene_io_contract|runtime_template|editor_inspector_interaction|editor_preview|editor_diagnostics|editor_template|editor_component_operations)_test$'
```

Start fresh windows with the mode variable unset, empty, and explicit `cached-linear`, verifying `cached-linear` in all three cases with zero grid storage or maintenance. Test retired-selector rejection (`indexed`, `current-linear`), unknown-mode rejection, and that disabling culling (`--no-render-culling` / `enableRenderCulling=false`) overrides a valid selection cleanly. Verify O(1) clean fixed-view reuse, O(dirty slots) stationary edits, O(N) camera-pan scans, and independent visibility membership and ordered submissions.

Cover no camera, empty population, disabled culling, headless execution, custom renderer target/viewport/scale, invalid or uncertifiable mappings, restoration to the supported mapping, and direct caller-ordered Camera draws. Unsupported mappings traverse the ordered all-sprite list. On numeric fallback verify the existing per-sprite eligibility rule and the latest Z on the first restored frame. Headless runs retain no renderer culling or ordering state.

Correctness and work assertions must include:

- Independent expected sequences and transparency composites for negative, zero, positive, minimum, and maximum Z values, different textures, equal-depth ties, edits, dense slot moves, and real unregister/re-register.
- With `cached-linear` culling, Z edits to slots excluded from a fixed prepared subset cause no draw-list rebuild, bounds refresh, or visibility query. When camera movement or fallback later includes an edited sprite, that first prepared frame uses its latest Z value.
- With clean retained keys, unchanged membership reuses prepared draw order in O(1) even when a full reselection re-enumerates the same set. Equal-sized sets with different members invalidate it. A changed selection performs no numeric-rank sort and at most one draw-key sort (up to O(V log V)).
- No numeric rank sorting or sorted delta merge remains in the new membership path. Validate inverse positions, delta swap-erase, same-set full queries, equal-count membership exchanges, registration, relocation, removal, and scene retirement.
- A retained Z edit may use the planned full draw-list rebuild. Compare one-entry repositioning only through the bounded prototype workload below. Verify the final total key order independently for both paths.
- Headless/renderer-free runs allocate and maintain no renderer-owned culling or ordering state. Scene replacement, failed preparation, and retired handles retain their lifetime guarantees.
- Legacy assets without Z Order load as zero. Valid signed endpoints round-trip. Invalid JSON types and out-of-range values use established boundary errors. Cloning, templates, and copies preserve the authored value.
- Preview, overlap picking, undo/redo, templates, and paused edits agree with runtime ordering. An ordering-only edit does not reload assets or refresh geometry.

CTest supplies the established dummy SDL video/audio and software renderer. Keep image and identity oracles in memory. Do not retain per-frame identity lists or images.

## 3. Manual authoring check

Open an existing scene in the editor. Set three overlapping sprites to -10, 0, and 10. Verify the frontmost sprite and overlap-pick cycle. Edit while paused, undo and redo, save and reopen, duplicate a sprite, then edit and revert a template override. Remove an unrelated sprite and confirm surviving ties retain their relative order.

## 4. Resource and timing builds

The root CMake configuration derives work probes from `BUILD_TESTING`. Build one Release resource probe with tests enabled and one Release timing probe with tests disabled. Enable the benchmark target in both, and disable sanitizers and editor build targets.

```sh
cmake -S . -B build/render-order-validation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_SANITIZER=none -DBUILD_TESTING=ON \
  -DPLATFORMATOR_BUILD_EDITOR=OFF -DPLATFORMATOR_BUILD_BENCHMARKS=ON \
  -DPLATFORMATOR_INSTALL=OFF
cmake --build build/render-order-validation --parallel 4 \
  --target platformator_linear_render_probe platformator_benchmark_render_culling_cli_test
ctest --test-dir build/render-order-validation --output-on-failure -R '^platformator_benchmark_render_culling_cli_test$'
cmake -S . -B build/render-order-timing -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_SANITIZER=none -DBUILD_TESTING=OFF \
  -DPLATFORMATOR_BUILD_EDITOR=OFF -DPLATFORMATOR_BUILD_BENCHMARKS=ON \
  -DPLATFORMATOR_INSTALL=OFF
cmake --build build/render-order-timing --parallel 4 \
  --target platformator_linear_render_probe
build/render-order-validation/platformator_linear_render_probe --print-build-identity
build/render-order-timing/platformator_linear_render_probe --print-build-identity
```

Verify that candidate identities match on source, fixture, harness, Release configuration, sanitizer setting, and expected work-probe setting. Reuse equivalent builds. Keep only identities, commands, summaries, and timing samples in the report.

## 5. One bounded combined pilot

Run `engine/benchmarks/capture_linear_render.py` with the `render-order-culling` comparison and deterministic probe Z schedules and work counters. It reuses the render strategies and scene fixture machinery while preserving the defaults of 3 independent repetitions, 5 warmup frames, 30 measured frames, a 50 MiB output limit, and a 600 second whole-run limit.

| Case | Workload | Comparison |
|---|---|---|
| `authored-static` | 64 all-visible sprites in a fixed view, all Z values zero | Candidate `cached-linear` (and optional frozen pre-removal `indexed` endpoint labeled as implementation + policy change). |
| `sparse-camera-pan` | Existing 10,000-sprite sparse `camera-motion` trajectory, all Z values zero | Candidate `cached-linear` (and optional frozen pre-removal `indexed` endpoint labeled as implementation + policy change; historical same-build indexed tradeoff archived separately). |
| `dense-z-edit` | 10,000 retained sprites, mixed Z values, fixed camera, deterministic draw-list order | Candidate full rebuild versus a forced per-frame full-sort reference and benchmark-only one-entry repair, plus multiworker control. Include clean and single-edit phases separately. |

The dense Z fixture uses 10,000 sprites placed across the 320×240 view with 1×1 render dimensions at the fixture's unit scale, so all sprites are retained while raster overdraw stays low. A changed entry must cross at least one peer in the total key. The schedule, selected slot, values, and resulting order are deterministic. Charge setter and prototype bookkeeping to paired step-plus-render timing under `cached-linear`. Verify that all 10,000 sprites are retained in every measured phase and include one matched multiworker control.

In the same bounded run, check rejected-only Z edits on the prepared `cached-linear` subset and capture the 10,000-sprite fully-moving-crossing workload under `cached-linear` as a resource/work control without extra timing matrices or retries.

The bounded comparison includes the resource-only `rejected-z-edit` and `fully-moving-crossing` controls automatically, so it does not add another timing matrix or capture run. Keep the invocation to the supported common arguments and the `--comparison` and `--focused-cases` options below; do not change limits, enable `--allow-full-campaign`, or create replacement artifact directories.

Primary candidate-only invocation (no `RENDER_BASELINE_PROBE` environment variable required):

```sh
RENDER_RUN_ID=replace-with-one-unused-run-id
RENDER_REPORT_ROOT="$PWD/specs/019-sprite-z-ordering/evidence/$RENDER_RUN_ID"
test ! -e "$RENDER_REPORT_ROOT"
SDL_RENDER_DRIVER=software python3 engine/benchmarks/capture_linear_render.py \
  --comparison render-order-culling \
  --focused-cases authored-static,sparse-camera-pan,dense-z-edit \
  --candidate-validation-probe "$PWD/build/render-order-validation/platformator_linear_render_probe" \
  --candidate-probe "$PWD/build/render-order-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/engine/benchmarks/assets" \
  --workers 1 --multiworker-count 4 --seed 0 \
  --output "$RENDER_REPORT_ROOT"
```

When comparing `authored-static` and `sparse-camera-pan` against a supplied frozen pre-removal executable (`RENDER_BASELINE_PROBE` set as in §1), add the optional baseline argument:

```sh
  --baseline-probe "$RENDER_BASELINE_PROBE"
```

All options above (`--comparison render-order-culling`, `--focused-cases`, optional `--baseline-probe`, `--workers`, `--multiworker-count`, `--seed`, probe paths, `--asset-root`, and `--output`) are supported CLI inputs. Run the Python harness tests after any driver change:

```sh
python3 -m unittest discover -s engine/benchmarks/tests -p 'test_linear_render.py' -v
```

Retain a compact report with build/workload identities, exact commands, raw timing samples, uncertainty, resource observations, and any partial-result limitation. Stop on a time, output, or correctness failure. Do not retry automatically.

## 6. Read the results

Report visible selection, ordering preparation, paired step-plus-render time, separate step and render scopes, initialization, typical and tail samples, allocation and retained bytes, and process variation without adding nested scopes together. Label any comparison against the frozen pre-change indexed endpoint as a combined implementation + policy change, and reference `pre-removal-grid-vs-cached-20261118` when discussing the historical sparse-pan grid tradeoff.

For the dense Z case, report no-edit and one-entry-changing phases independently. Include draw entries visited, full sorts, one-entry repositions, setter-side work, and retained capacity. Report the fully moving work/resource observations separately from timing rows. A short pilot informs these selected workloads and cannot establish universal performance or GPU timing. If a baseline is unavailable or run variation obscures a difference, report that limitation directly.
