# Compact rendering performance comparisons

The default is a focused exploratory pilot. The historical exhaustive acceptance campaign is suspended; unchecked campaign tasks are not instructions to restart it. See the repository [benchmark policy](../../AGENTS.md).

The user deleted the two October 29 artifact bundles on October 30, along with `bin/` and `build/`. The written [results](evidence/RESULTS.md) remain historical observations. Their raw captures, saved binaries and replay/verifier scripts are no longer available. Do not regenerate that archive automatically.

## Build only what is needed

Keep builds outside report directories. For resource observations use a Release work-probe build; for timing use a separate Release build without work probes, debug tools or sanitizers:

```sh
cmake -S . -B build/render-validation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON \
  -DPLATFORMATOR_ENABLE_WORK_PROBES=ON -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF \
  -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_BUILD_EXAMPLES=OFF \
  -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none
cmake --build build/render-validation --target platformator_linear_render_probe --parallel 4

cmake -S . -B build/render-timing -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON \
  -DPLATFORMATOR_ENABLE_WORK_PROBES=OFF -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF \
  -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF \
  -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none
cmake --build build/render-timing --target platformator_linear_render_probe --parallel 4
```

Use the executable paths printed by the build (the project's output layout may place them under `bin/`). Do not copy builds, binaries, libraries or source trees into the reports.

## Run a compact pilot

```sh
python3 benchmark/capture_linear_render.py \
  --candidate-validation-probe /path/to/work-probe/platformator_linear_render_probe \
  --candidate-probe /path/to/timing/platformator_linear_render_probe \
  --asset-root "$PWD/assets" --multiworker-count 6 \
  --output benchmark/artifacts/linear-render-culling/pilot-<new-id>
```

Replace the paths and run ID. The default selects five representative cases, all three culling strategies, 3 independent processes per strategy, 5 warmup frames and 30 measured frames. Add `crossing-multiworker` to `--focused-cases` only when investigating worker differences. The pilot always identifies itself as exploratory; it does not establish full correctness or feature acceptance.

Output is a JSON report, raw timing CSVs/manifests, small resource observations and logs. No per-frame sprite lists or image dumps are produced. Preserve those small files together so the report's sample links remain useful.

The driver enforces **50 MiB combined new output** and **600 seconds elapsed capture time**, including probe identity inspection. Child processes are checked every 250 ms; a fast writer can temporarily overshoot the size threshold. Limits include a separate generated validation/resource root when applicable, but exclude existing input evidence and builds. Build size/time is outside the capture budget.

On a limit breach, the driver stops its owned child process group, returns failure, retains partial files and removes success markers. Inspect the failure; do not automatically retry or raise the limits. The limits must be finite and positive. `--max-output-mib` and `--max-seconds` exist for explicitly agreed experiments, not automatic escalation.

## Detailed validation

Use targeted CTest correctness tests for geometry, ordering, motion, invalidation and lifecycle checks. Keep timing runs free of work probes.

Historical non-pilot phases and the legacy comparison now require `--allow-full-campaign`; their larger frame/repetition counts must also be supplied explicitly. They remain available for a specifically requested experiment, but no full campaign or large archive is authorized by this quickstart. Existing performance acceptance gaps remain open; do not relabel a short pilot as complete acceptance evidence.
