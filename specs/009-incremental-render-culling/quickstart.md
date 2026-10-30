# Incremental Render Culling Validation and Pilot

The implementation and the incremental capture driver are available. Use the focused pilot first to screen workloads; it is explicitly exploratory and does not replace correctness proofs or the complete acceptance campaign. The current measured run and its limits are in [RESULTS.md](evidence/RESULTS.md), with workload coverage in [coverage.md](evidence/coverage.md).

## Durable run root and reference

Run from the repository root. T044's verified feature-008 source/archive remains at `benchmark/artifacts/linear-render-culling/009-20261029-run-01/reference/`; do not rebuild or overwrite it for the pilot. Choose a fresh ignored root for a new run:

```sh
cd /home/user/Projects/Platformator
RUN_ROOT="$PWD/benchmark/artifacts/linear-render-culling/009-<new-run-id>"
git check-ignore -v "$RUN_ROOT"
test ! -e "$RUN_ROOT"
mkdir -p "$RUN_ROOT"
```

The retained 2026-10-29 pilot bundle is `benchmark/artifacts/linear-render-culling/009-20261029-run-01/`. Its checksums, raw sample links, command, verifier, and independent relocation check are indexed by `pilot-final-index.json`.

## Candidate builds

Use separate Release builds. The validation probe needs work probes; timing must disable work probes, sanitizers, and debug tools. The repository-local build directories below avoid copying a second full source/build tree into the artifact bundle. Copy the resulting binaries, build identities, and source-input manifests into the chosen run root as distinct files before capture.

```sh
cmake -S . -B build/009-incremental-validation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF \
  -DPLATFORMATOR_ENABLE_BENCHMARKS=ON \
  -DPLATFORMATOR_ENABLE_WORK_PROBES=ON \
  -DPLATFORMATOR_BUILD_TESTS=ON \
  -DPLATFORMATOR_BUILD_EXAMPLES=OFF \
  -DPLATFORMATOR_INSTALL=OFF \
  -DPLATFORMATOR_SANITIZER=none
cmake --build build/009-incremental-validation --target \
  platformator_render_culling_test platformator_render_culling_trajectory_test \
  platformator_linear_render_probe --parallel 4
ctest --test-dir build/009-incremental-validation \
  -R 'render_culling|linear_render_python' --output-on-failure

cmake -S . -B build/009-incremental-timing -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF \
  -DPLATFORMATOR_ENABLE_BENCHMARKS=ON \
  -DPLATFORMATOR_ENABLE_WORK_PROBES=OFF \
  -DPLATFORMATOR_BUILD_TESTS=OFF \
  -DPLATFORMATOR_BUILD_EXAMPLES=OFF \
  -DPLATFORMATOR_INSTALL=OFF \
  -DPLATFORMATOR_SANITIZER=none
cmake --build build/009-incremental-timing \
  --target platformator_linear_render_probe --parallel 4
```

Verify `--print-build-identity` for both probes. Source, fixture, and harness identities must match. Confirm the timing build is Release with work probes, debug tools, and sanitizers disabled. The candidate source and asset hashes are retained with the report. The broader public consumer, sanitizer, lifecycle, mapping, numeric, and failure-injection gates remain separate checks; consult [final-validation.md](evidence/final-validation.md) for results and limitations.

## Focused exploratory pilot

The pilot needs only a candidate work-probe executable, a candidate uninstrumented timing executable, an asset root, and a fresh output directory. It does not require full phase roots or frozen-reference probes. The six representative cases below include one selected six-worker crossing run:

```sh
RUN_ROOT="$PWD/benchmark/artifacts/linear-render-culling/009-<new-run-id>"
SDL_RENDER_DRIVER=software python3 benchmark/capture_linear_render.py \
  --comparison incremental --phase pilot --strategy all --exploratory \
  --focused-cases mostly-offscreen,camera-motion,small-moving-subset,fully-moving-crossing,all-visible,crossing-multiworker \
  --candidate-validation-probe "$RUN_ROOT/candidate/build-validation/platformator_linear_render_probe_pilot" \
  --candidate-probe "$RUN_ROOT/candidate/build-timing/platformator_linear_render_probe_pilot" \
  --asset-root "$PWD/assets" --workers 1 --multiworker-count 6 --seed 0 \
  --repetitions 3 --warmup-frames 5 --measure-frames 30 \
  --dt 0.008333333333333333 --output "$RUN_ROOT/captures/pilot"
```

Adjust the executable names to the copies stored in the new root. The six case names and exact resolved parameters are recorded in the report. The driver shuffles case/strategy process order reproducibly, stores all raw CSV/sample/manifest/log files, and refuses to reuse an output path. Cancellation stops the owned process group and cannot write a complete success report.

This short run records resource setup/growth/reset observations for current-linear, cached-linear, indexed, and disabled modes, then compares the three candidate strategies in uninstrumented timing. It deliberately emits no per-frame image/order traces and no frozen feature-008 controls. It is a screening tool only. Increase repetitions or select larger populations only to resolve a stated uncertainty; do not claim acceptance from the pilot.

### Re-run the retained pilot

The retained pilot has a separate, hash-checked input set at `candidate/source/pilot-inputs/`. It contains the 151 source, harness, and asset files from the final pilot build, including the matching `capture_linear_render.py` and its `sweep_scaling.py` dependency. The file hashes and aggregate are copied from `candidate/build-timing/proof/pilot-source-inputs-final.manifest`. The older `candidate/source/Platformator/` snapshot predates ten of those inputs and is not the source for reproducing the pilot.

To validate the saved report and raw measurements, run the evidence verifier. To execute a new exploratory pilot with the saved binaries, libraries, driver, and assets, give the capture script a fresh output directory:

```sh
BUNDLE="$PWD/benchmark/artifacts/linear-render-culling/009-20261029-run-01"
"$BUNDLE/scripts/verify_pilot.sh"
"$BUNDLE/commands/pilot-capture.sh" "$BUNDLE/captures/pilot-replay-<new-run-id>"
```

The capture script verifies all 151 input hashes and the aggregate against the recorded timing build identity before it starts. It rejects an existing output path. Use `--dry-run` to check the manifest, output path, and bundled SDL/oneTBB library resolution without creating output or starting probes:

```sh
"$BUNDLE/commands/pilot-capture.sh" --dry-run /tmp/platformator-pilot-dry-run
```

The two probe launchers select their matching bundled SDL and oneTBB libraries, so the rerun does not depend on a live checkout or its local build directories. The host still needs a compatible Linux system runtime; see the recorded OS and dependency details in [capture-index.md](evidence/capture-index.md).

Rebuilding from source is a separate operation. Use `candidate/source/pilot-inputs/` as the exact 151-file overlay for the saved pilot identity on a clean Platformator checkout, verify the aggregate in the manifest after applying it, and follow the configure/build commands above with a compatible compiler and system dependencies. This archive predates the later memory-observer accounting correction in the current workspace. The overlay preserves the pilot's fingerprinted inputs, not Git history or non-fingerprinted repository metadata. A saved-binary rerun does not rebuild or establish new source-build evidence.

## Full correctness and acceptance evidence

The original `validate`, `resources`, and `time` phases remain available for acceptance work. The full contract requires corresponding-frame image/order/oracle evidence, matching resource proofs, selected frozen-008 and stock-engine controls, at least five independent timing processes, and the full prescribed workload inventory. It includes 50k scale, giant/rotated/skewed scenes, output/numeric fallback, scenes, and inactive controls. Its unchecked tasks are not complete based on a pilot.

For a completed pilot bundle, run:

```sh
"$RUN_ROOT/scripts/verify_pilot.sh"
```

That checker validates pilot files and independently recomputes raw summaries and process-level aggregates using bundle-relative paths. It does not close the full-bundle relocation gate or SC-006. Full feature acceptance requires evidence and review documented in [validation-evidence.md](contracts/validation-evidence.md).
