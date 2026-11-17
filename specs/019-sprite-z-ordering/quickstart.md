# Validation Guide: Render Culling Cleanup and Sprite Z Ordering

Run these checks after implementing the combined culling and ordering changes. The feature tests, probe workloads, and driver comparison described here are planned additions. Planning has not modified runtime code or run a benchmark.

## 1. Record the pre-change baseline

From the repository root, use the current CMake/Ninja, C++20, Python 3, and vendored dependencies. Read `AGENTS.md` and `engine/benchmarks/README.md`.

Set `RENDER_BASELINE_PROBE` to the absolute path of the pre-change timing executable. Before runtime changes, record the current source revision, dirty source fingerprint, and Release timing-probe identity. Reuse a matching build if available. Otherwise build the existing probe once under `build/`. Keep binaries and build trees outside the report directory.

Set `RENDER_BASELINE_PROBE` to the absolute path of that recorded pre-change probe. It supplies the indexed-culling control for the all-zero authored-scale and 10,000-sprite pan cases. It has no Z-order options. Record its workload and build identity. On the candidate, run both indexed and cached-linear modes against the same resolved workload. This gives the sequence old indexed, candidate indexed, candidate cached-linear. The first comparison measures combined implementation overhead with the culling strategy held fixed, including both the culling cleanup and the ordering work. The second compares culling strategies within one candidate build. Report any remaining attribution uncertainty within this pilot. A missing equivalent baseline remains unavailable rather than an assumed zero-cost result.

## 2. Focused correctness and work checks

Configure the standard test preset, build the relevant runtime/editor targets, and run the selected tests:

```sh
cmake --preset test
cmake --build --preset test --parallel 4 --target \
  platformator_sprite_ordering_test \
  platformator_render_culling_test platformator_render_culling_index_test \
  platformator_render_culling_worker_test platformator_render_culling_trajectory_test \
  platformator_render_culling_contract_test \
  platformator_scene_io_contract_test platformator_runtime_template_test \
  platformator_editor_inspector_interaction_test platformator_editor_preview_test \
  platformator_editor_diagnostics_test platformator_editor_template_test \
  platformator_editor_component_operations_test
ctest --test-dir build/test --output-on-failure \
  -R '^platformator_(sprite_ordering|render_culling|render_culling_index|render_culling_worker|render_culling_trajectory|render_culling_contract|scene_io_contract|runtime_template|editor_inspector_interaction|editor_preview|editor_diagnostics|editor_template|editor_component_operations)_test$'
```

Start fresh windows with the mode variable unset and empty, verify cached-linear in both cases, and check zero grid-specific retained bytes and relinks after registration and growth. Test unknown-mode rejection and that disabling culling overrides each valid selection. Run explicit current-linear, cached-linear, and indexed controls on the same spread, clustered, and moving fixtures. Verify that indexed remains available as an explicit path. Compare visibility membership and ordered submissions against independent oracles.

Cover no camera, empty population, disabled culling, headless execution, custom renderer target/viewport/scale, invalid or uncertifiable mappings, restoration to the supported mapping, and direct caller-ordered Camera draws. Unsupported mappings traverse the ordered all-sprite list. On numeric fallback verify the existing per-sprite eligibility rule and the latest Z on the first restored frame. Headless runs retain no renderer culling or ordering state.

Correctness and work assertions must include:

- Independent expected sequences and transparency composites for negative, zero, positive, minimum, and maximum Z values, different textures, equal-depth ties, edits, dense slot moves, and real unregister/re-register.
- In either cached policy, Z edits to slots excluded from a fixed prepared subset cause no draw-list rebuild, bounds refresh, spatial migration, or visibility query. When camera movement or fallback later includes an edited sprite, that first prepared frame uses its latest Z value.
- With clean retained keys, unchanged membership reuses prepared draw order even when a visibility query enumerates the same set in a different order. Equal-sized sets with different members invalidate it. A changed indexed membership set performs no numeric-rank sort and at most one draw-key sort.
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

Extend `engine/benchmarks/capture_linear_render.py` with a focused `render-order-culling` comparison and the probe with deterministic Z schedules and work counters. Reuse the existing render strategies and scene fixture machinery. The driver extension must preserve the defaults of 3 independent repetitions, 5 warmup frames, 30 measured frames, a 50 MiB output limit, and a 600 second whole-run limit.

| Case | Workload | Comparison |
|---|---|---|
| `authored-static` | 64 all-visible sprites in a fixed view, all Z values zero | Pre-change indexed, candidate indexed, and candidate cached-linear. |
| `sparse-camera-pan` | Existing 10,000-sprite sparse `camera-motion` trajectory, all Z values zero | Pre-change indexed, candidate indexed, and candidate cached-linear. |
| `dense-z-edit` | 10,000 retained sprites, mixed Z values, fixed camera, deterministic draw-list order | Candidate full rebuild versus a forced per-frame full-sort reference. In the one-visible-edit phase also compare the one-entry reposition prototype. Include no-edit and one-entry-changing phases as separate samples. |

The dense Z fixture is a planned probe workload with 10,000 sprites placed across the 320×240 view with 1×1 render dimensions at the fixture’s unit scale, so all sprites are retained while raster overdraw stays low. A changed entry must cross at least one peer in the total key. The schedule, selected slot, values, and resulting order are deterministic. Charge setter and prototype bookkeeping to paired step-plus-render timing. Use cached-linear for all dense-Z comparisons. Verify that all 10,000 sprites are retained in every measured phase. Include one matched multiworker candidate control for that case.

In the same bounded run, check rejected-only Z edits on the two prepared cached subsets. Capture the existing 10,000-sprite fully-moving-crossing workload as a resource/work control under current-linear, cached-linear, and indexed. These controls show update-side and retained-state costs without adding separate timing campaigns. Work observations distinguish bounds refreshes, index migration, candidate visits, membership changes, sorting, retained capacities, allocations, and fallback work. Correctness tests separately cover projection rebuilds, churn, large Z edit fractions, and no-culling fallback.

The comparison selector and case names are planned additions. The bounded comparison includes a resource-only fully-moving-crossing control, so it does not add another timing matrix or capture run. Keep the invocation to existing common arguments and the planned comparison selector and focused cases. Do not pass unimplemented case-specific switches, change limits, enable `--allow-full-campaign`, or create replacement artifact directories.

Example invocation after that driver extension is implemented:

```sh
RENDER_RUN_ID=replace-with-one-unused-run-id
RENDER_REPORT_ROOT="$PWD/specs/019-sprite-z-ordering/evidence/$RENDER_RUN_ID"
test ! -e "$RENDER_REPORT_ROOT"
SDL_RENDER_DRIVER=software python3 engine/benchmarks/capture_linear_render.py \
  --comparison render-order-culling \
  --focused-cases authored-static,sparse-camera-pan,dense-z-edit \
  --baseline-probe "$RENDER_BASELINE_PROBE" \
  --candidate-validation-probe "$PWD/build/render-order-validation/platformator_linear_render_probe" \
  --candidate-probe "$PWD/build/render-order-timing/platformator_linear_render_probe" \
  --asset-root "$PWD/engine/benchmarks/assets" \
  --workers 1 --multiworker-count 4 --seed 0 \
  --output "$RENDER_REPORT_ROOT"
```

The comparison selector and case names above are new planned driver inputs. Existing `--workers`, `--multiworker-count`, `--seed`, probe, asset-root, and output options are already part of the rendering capture workflow. Run the existing Python harness tests after implementing the driver extension:

```sh
python3 -m unittest discover -s engine/benchmarks/tests -p 'test_linear_render.py' -v
```

Retain a compact report with build/workload identities, exact commands, raw timing samples, uncertainty, resource observations, and any partial-result limitation. Stop on a time, output, or correctness failure. Do not retry automatically.

## 6. Read the results

Report paired step-plus-render time, separate step and render scopes, initialization, typical and tail samples, allocation and retained bytes, and process variation. Do not add nested scopes together. Keep the pre-change-to-candidate indexed comparison separate from the candidate indexed-to-cached-linear comparison. The former measures the combined implementation overhead under the old culling strategy. The latter measures the culling-mode difference on one source build.

For the dense Z case, report no-edit and one-entry-changing phases independently. Include draw entries visited, full sorts, one-entry repositions, setter-side work, and retained capacity. Report the fully moving work/resource observations separately from timing rows. A short pilot informs these selected workloads and cannot establish universal performance or GPU timing. If a baseline is unavailable or run variation obscures a difference, report that limitation directly.
