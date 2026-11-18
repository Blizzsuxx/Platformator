# Render Order & Culling Pilot Report

- Repetitions: 3, Warmup: 5, Measured: 30
- Baseline binary: `/home/user/Projects/Platformator/build/render-order-baseline/platformator_linear_render_probe`
- Candidate timing: `/home/user/Projects/Platformator/build/render-order-timing/platformator_linear_render_probe`
- Candidate validation: `/home/user/Projects/Platformator/build/render-order-validation/platformator_linear_render_probe`

## Case: authored-static

| Arm | Slice | Paired Mean (ms) | Render Mean (ms) | Step Mean (ms) | Paired p95 (ms) |
|---|---|---:|---:|---:|---:|
| baseline-indexed | combined | 16.6637 | 16.6526 | 0.0035 | 16.6693 |
| candidate-indexed | combined | 16.6335 | 16.6227 | 0.0035 | 16.6691 |
| candidate-cached-linear | combined | 16.6581 | 16.6464 | 0.0039 | 16.6899 |

## Case: sparse-camera-pan

| Arm | Slice | Paired Mean (ms) | Render Mean (ms) | Step Mean (ms) | Paired p95 (ms) |
|---|---|---:|---:|---:|---:|
| baseline-indexed | combined | 16.6634 | 16.6518 | 0.0045 | 16.6687 |
| candidate-indexed | combined | 16.6413 | 16.6307 | 0.0048 | 16.6692 |
| candidate-cached-linear | combined | 16.6416 | 16.6296 | 0.0054 | 16.6770 |

## Case: dense-z-edit

| Arm | Slice | Paired Mean (ms) | Render Mean (ms) | Step Mean (ms) | Paired p95 (ms) |
|---|---|---:|---:|---:|---:|
| candidate-rebuild-w1 | combined | 16.6396 | 16.6290 | 0.0040 | 16.6697 |
| candidate-rebuild-w1 | clean_frames | 16.6119 | 16.6011 | 0.0038 | 16.6694 |
| candidate-rebuild-w1 | edit_frames | 16.6673 | 16.6568 | 0.0043 | 16.6739 |
| candidate-forced-sort-w1 | combined | 16.6346 | 16.6215 | 0.0050 | 16.6691 |
| candidate-forced-sort-w1 | clean_frames | 16.6249 | 16.6117 | 0.0047 | 16.6686 |
| candidate-forced-sort-w1 | edit_frames | 16.6442 | 16.6312 | 0.0053 | 16.6696 |
| candidate-repair-w1 | combined | 16.6431 | 16.6296 | 0.0048 | 16.6685 |
| candidate-repair-w1 | clean_frames | 16.6393 | 16.6257 | 0.0043 | 16.6685 |
| candidate-repair-w1 | edit_frames | 16.6470 | 16.6336 | 0.0052 | 16.6682 |
| candidate-rebuild-w4 | combined | 16.6417 | 16.6304 | 0.0044 | 16.6689 |
| candidate-rebuild-w4 | clean_frames | 16.6087 | 16.5971 | 0.0042 | 16.6709 |
| candidate-rebuild-w4 | edit_frames | 16.6746 | 16.6636 | 0.0045 | 16.6685 |

## Interpretation, Final Choice & Verification Summary

- **Final implementation choice**: Keep `DefaultRebuild` (`O(V log V)` rebuild-and-sort over active visible slots) as the production default in [sdlwindow.cpp](file:///home/user/Projects/Platformator/engine/src/sdlwindow.cpp), isolating `ForcedFullSort` and `SingleEntryRepair` behind `PLATFORMATOR_ENABLE_BENCHMARK_ORDER_MODES` (`T055`, omitted from non-test/non-benchmark `libplatformator.a` builds). Default culling uses `cached-linear`, reserving `indexed` for opt-in use.
- **Paired run spread & software VSync limitation**: Under SDL `renderer = "software"` with `vsync = 1`, frame duration stays pinned near `~16.6 ms` (~60 Hz pacing) so sub-millisecond preparation shifts fall inside presentation pacing jitter:
  - `authored-static`: implementation overhead (`candidate-indexed` vs `baseline-indexed`) `[-0.0255, -0.0256, -0.0397] ms` (mean `-0.0303 ms`, stdev `0.0082 ms`); culling strategy (`candidate-cached-linear` vs `candidate-indexed`) `[+0.0533, +0.0031, +0.0175] ms` (mean `+0.0246 ms`, stdev `0.0259 ms`); default policy (`candidate-cached-linear` vs `baseline-indexed`) `[+0.0278, -0.0226, -0.0221] ms` (mean `-0.0056 ms`, stdev `0.0290 ms`).
  - `sparse-camera-pan`: implementation overhead `[-0.0240, -0.0219, -0.0203] ms` (mean `-0.0220 ms`, stdev `0.0018 ms`); culling strategy `[+0.0014, +0.0015, -0.0018] ms` (mean `+0.0003 ms`, stdev `0.0019 ms`); default policy `[-0.0226, -0.0203, -0.0222] ms` (mean `-0.0217 ms`, stdev `0.0012 ms`).
  - `dense-z-edit`: on `edit_frames`, `candidate-repair-w1` vs `candidate-rebuild-w1` spread is `[+0.0331, -0.0930, -0.0009] ms` (mean `-0.0203 ms`, median `-0.0009 ms`, stdev `0.0652 ms`); `candidate-rebuild-w1` vs `candidate-forced-sort-w1` is `[+0.0694, -0.0002, +0.0001] ms` (mean `+0.0231 ms`, median `+0.0001 ms`, stdev `0.0401 ms`); on `clean_frames`, `candidate-rebuild-w1` vs `candidate-forced-sort-w1` is `[-0.0194, -0.0141, -0.0054] ms` (mean `-0.0130 ms`, median `-0.0141 ms`, stdev `0.0070 ms`).
- **Ordinary cold allocations/bytes & steady-state memory**:
  - `authored-static`: `candidate-indexed` construction/registration took `7.17 ms` (`1,059` allocs, `809,233 B`), retaining `41,992 B` culling (`35,336 B` grid) + `2,048 B` order; `candidate-cached-linear` took `7.77 ms` (`767` allocs, `752,041 B`), retaining `6,656 B` culling (`0 B` grid) + `2,048 B` order. First warmup step (`step 0`) and all `30` measured frames performed `0` C++ heap allocations (`0 B`) in both arms.
  - `sparse-camera-pan` (`10,000` sprites): `candidate-indexed` construction/registration took `28.83 ms` (`145,969` allocs, `28,749,225 B`), retaining `10,762,072 B` culling (`9,058,136 B` grid) + `524,288 B` order; `candidate-cached-linear` took `16.16 ms` (`80,341` allocs, `13,782,081 B`), retaining `1,703,936 B` culling (`0 B` grid) + `524,288 B` order. First warmup step took `1.008 ms` vs `0.714 ms` with `0` warmup step allocations and `0` measured-frame allocations.
  - `dense-z-edit` (`10,000` visible): `cached-linear` arms spent `16.47–18.69 ms` in construction/registration (`80,341` allocs, `13,782,081 B`), retaining `1,703,936 B` culling (`0 B` grid) + `524,288 B` order, with `0` warm allocations across all measured frames.
- **Sparse candidate visits**: Over `30` pan frames, `candidate-indexed` ran `30` spatial queries visiting `5,952` candidates (`0` full-population scans, `0` bounds refreshes, `0` relinks); `candidate-cached-linear` ran `0` spatial queries and `300,000` conservative linear visits (`full_population_visits = 300,000`). Both ran `26` sorts over `1,442` entries (`10,110` vs `7,002` comparator calls) and incurred `0` index relinks (`20,186` index relinks on `indexed` vs `0` on `cached-linear` in the `10k` crossing control).
- **Dense clean/edit phase work & rejected control**:
  - `dense-z-edit` (`15` clean / `15` edit frames): `candidate-rebuild-w1` and `candidate-rebuild-w4` performed `0` preparations/sorts/entries on clean frames (`15` cache hits) and `15` preparations/sorts (`150,000` entries, `2,483,196` comparisons) on edit frames. `candidate-forced-sort-w1` sorted every frame (`30` sorts, `300,000` entries, `4,970,238` comparisons). `candidate-repair-w1` performed `0` work on clean frames and `15` repairs (`0` sorts, `15` rewritten entries, `110,775` shifted moves, `213` comparator calls) on edit frames.
  - `rejected-z-edit` (`10k`): `0` sorts, `0` preparations, `0` bounds refreshes, `0` relinks, `0` queries, and `0` warm allocations on both `indexed` and `cached-linear`.
- **Original source fingerprint scope & offline manifest verification (`30` timed manifests)**:
  - All retained `report.json`, raw CSV, manifest, and resource files in this directory belong to the unmodified pre-converge pilot capture from commit `348d0d7842f6c993e2ca67ec30c8bfdc8ec9518e` (`candidate source_sha256: d440ce5ad6cbeb91634c07776ad5f7c007df6dd96cd3c42de97299958ddae8a3`, `harness_sha256: c75bb488ff070e036f4ecee05e54e4b92f9b32f8b6735f803732fdaef659cd99`, `fixture_sha256: 170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`). Subsequent convergence updates (`T047–T068`) were verified by post-edit unit, CTest, and small work-probe runs without replacing these historical files.
  - Offline inspection enumerated all `30` `raw/*.manifest.json` files (`10` case-arms × `3` repetitions `r0..r2`):
    - **Frozen baseline, 1 worker (`6` manifests)**: `source_sha256 = ae1f6003dc402c7243fb91d9cb91e161c86b9b376a73b3572fbe8881759c021e`, `harness_sha256 = c1345c9ec9de4e08d1942b904cd293d1d930b73fa384bd49990001a048f1d62a`, `workers = 1` — `authored-static-baseline-indexed-r{0..2}` (`3`, `sprite_count = 64`, `layout = fixed-area`, `camera_schedule = fixed`, `effective_policy = indexed`) and `sparse-camera-pan-baseline-indexed-r{0..2}` (`3`, `sprite_count = 10000`, `layout = constant-density-spread`, `camera_schedule = continuous-pan-240`, `effective_policy = indexed`).
    - **Candidate, 1 worker (`21` manifests)**: `source_sha256 = d440ce5ad6cbeb91634c07776ad5f7c007df6dd96cd3c42de97299958ddae8a3`, `harness_sha256 = c75bb488ff070e036f4ecee05e54e4b92f9b32f8b6735f803732fdaef659cd99`, `workers = 1` — `authored-static-candidate-indexed-r{0..2}` (`3`, `indexed`), `authored-static-candidate-cached-linear-r{0..2}` (`3`, `cached-linear`), `sparse-camera-pan-candidate-indexed-r{0..2}` (`3`, `indexed`), `sparse-camera-pan-candidate-cached-linear-r{0..2}` (`3`, `cached-linear`), `dense-z-edit-candidate-rebuild-w1-r{0..2}` (`3`, `cached-linear`, `ordering_variant = rebuild`), `dense-z-edit-candidate-forced-sort-w1-r{0..2}` (`3`, `cached-linear`, `ordering_variant = forced-sort`), and `dense-z-edit-candidate-repair-w1-r{0..2}` (`3`, `cached-linear`, `ordering_variant = repair`).
    - **Candidate, 4 workers (`3` manifests)**: `source_sha256 = d440ce5ad6cbeb91634c07776ad5f7c007df6dd96cd3c42de97299958ddae8a3`, `harness_sha256 = c75bb488ff070e036f4ecee05e54e4b92f9b32f8b6735f803732fdaef659cd99`, `workers = 4` — `dense-z-edit-candidate-rebuild-w4-r{0..2}` (`3`, `cached-linear`, `ordering_variant = rebuild`).
    - Verified across all `30` manifests: matching `fixture_sha256`, `Release` (`sanitizer = none`, `work_probes = false`, `debug_tools = false`), `software` renderer with `vsync = 1`, `seed = 0`, `warmup_steps = 5`, `measured_steps = 30`, `timing_eligible = true`, `complete_capture = true`, and identical `resolved_parameters` across repetitions and across compared arms within each case (`authored-static`, `sparse-camera-pan`, `dense-z-edit`). All `24` candidate manifests recorded `observed_frame_fallbacks = 0` (not emitted in the `6` frozen baseline manifests).
- **Explicitly unavailable measurements**:
  - Historical pilot capture omits `draw_order_prepare_ms` (added post-pilot in `T052`) and `sprite_order_lookup_visits` (added post-pilot in `T053`).
  - Pre-change frozen baseline build omits `draw_order_prepare_ns` and `observed_frame_fallbacks`.
  - Unpaced (`vsync = 0`) CPU saturation timings and hardware GPU timings were not collected (`software` renderer with `vsync = 1` only).
- **Authoring, post-converge & consumer checks (`T046` & `T047–T068`)**:
  - `14/14` §2 focused engine/editor CTests passed (`58.19s` on `build/test`) covering rendering compositing/culling/warm-hit/textureless/fallback, fused single-handle delta resolution, late-position & 3-mode ordering parity, allocation-failure retry, preview component-ID tie precedence, asset-reload draw-order preservation, signed-32 bounds, Z-only transform skipping, and Inspector Z editing/repair.
  - `platformator_package_smoke` passed (`28.77s` on `build/test`) after repairing [package_smoke.cmake](file:///home/user/Projects/Platformator/engine/tests/package_smoke.cmake) to invoke the `mario` executable with `--scene assets/scenes/level1.scene --smoke-steps 5` in both build-tree and relocated-install checks, alongside the installed/relocated/source `engine/tests/consumer/main.cpp` `Sprite::getZOrder`/`setZOrder` checks.
  - `3/3` benchmark probe CTests passed on `build/render-order-validation` (`platformator_benchmark_render_culling_cli_test`, `platformator_linear_render_allocation_observer_test`, `platformator_linear_render_z_oracle_test` including rejected-edit/crossing oracle checks), plus direct small-population `rejected-z-edit` and `crossing` `--validate-resources` checks and `45/45` Python driver tests in [test_linear_render.py](file:///home/user/Projects/Platformator/engine/benchmarks/tests/test_linear_render.py).
  - Ordinary Release `platformator_engine` library build (`build/lib-release`, `BUILD_TESTING=OFF`, `PLATFORMATOR_BUILD_BENCHMARKS=OFF`, `PLATFORMATOR_BUILD_EDITOR=OFF`) compiled cleanly (`T055`, `T067`).
  - **Open evidence (`T046` kept unchecked in [tasks.md](file:///home/user/Projects/Platformator/specs/019-sprite-z-ordering/tasks.md))**: Interactive §3 desktop editor walkthrough ([quickstart.md](file:///home/user/Projects/Platformator/specs/019-sprite-z-ordering/quickstart.md#L49-L51)) remains a manual GUI check and was not interactively launched in a desktop window (automated headless editor CTests cover programmatic flows).
