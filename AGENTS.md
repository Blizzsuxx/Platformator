# Benchmark artifact policy

Benchmark results must stay compact. This applies to all benchmark tools and overrides historical instructions to retain exhaustive campaigns or entire source/build bundles.

- Default to a focused performance comparison, not the full workload matrix. Do not restart unchecked historical acceptance campaigns automatically.
- Use `benchmark/capture_linear_render.py` for rendering comparisons. Its default is an exploratory pilot: 3 repetitions, 5 warmup frames, 30 measured frames, a 50 MiB output budget and a 600 second time budget.
- Do not increase limits or enable `--allow-full-campaign` without an explicit user request for that larger experiment. Other benchmark tools must also be run with equivalent size/time monitoring; they do not inherit the rendering driver's guards.
- Retain a small report, workload/build identities, commands and raw timing samples. Do not retain per-frame sprite identity lists, full-frame images, source archives, executables, shared libraries, or build trees by default. Use existing targeted correctness tests. Capture detailed diagnostic data only for a specific failure and with a small explicit bound.
- Keep builds under `build/` or `bin/`, outside report directories. Reuse builds when appropriate. Do not copy generated build trees or the repository into artifacts.
- Stop on budget/failure, report the partial result, and diagnose it. Do not automatically retry, expand the matrix, rebuild repeatedly, or create replacement artifact directories.
- Historical reports whose raw artifacts were deleted remain historical observations; do not claim their checksums or raw samples can still be verified.
