# Corrected C++ cleanup reference

Final source fingerprint: `9fad5294d11a084a705051a0877aedf84a56e14f38a28eecfae815068581af93`. Commit `4d5bc02ce03e4119fdc4a764e993d4d4b7dad35b` plus the archived dirty working tree. This is the new schema-v2 reference after correctness and lifecycle changes.

All 18 workload sizes pass per-step validation in one-/six-worker fresh processes (36 validation runs). Final body/contact snapshots agree within predeclared tolerances; IDs, supports, sleep and feature identities match exactly. The 500-body fixture exposed an empty clipped-box manifold, now covered by a failing-then-passing regression. Unused whole-contact sorting was removed before this capture; solver and observed-event ordering remains canonical.

## Complete-step reference

Values are **mean ± sample standard deviation across five independent process means**, in milliseconds per complete step. All sizes and per-run median/min/max/spread are in [summary.csv](summary.csv). These are headless runs with zero rendering samples.

| Workload | Largest requested size | 1 worker (ms) | 6 workers (ms) |
| --- | ---: | ---: | ---: |
| Broad lanes | 1,000 | 0.608 ± 0.057 | 0.208 ± 0.010 |
| Dense broad boxes | 300 | 2.653 ± 0.017 | 2.479 ± 0.034 |
| Narrow elements (4 colliders each) | 200 | 34.114 ± 0.861 | 17.365 ± 0.588 |
| Rigid bodies (+4 walls) | 1,000 | 1.513 ± 0.023 | 1.167 ± 0.030 |
| Static churn (steady 1,000) | 1,000 | 2.735 ± 0.054 | 1.401 ± 0.032 |
| Moving churn (steady 1,000) | 1,000 | 2.845 ± 0.046 | 1.437 ± 0.042 |

## Reproduction and noise

- Six scenarios × three sizes × two worker limits × five independent processes = **180 timing runs**, 108,000 measured steps. Each process warms 120 steps, measures 600 at dt=1/120, seed 0. Job order is shuffled with seed 1.
- One worker is pinned to logical CPU 0; six workers to 0,2,4,6,8,10. AMD Ryzen 5 7535HS, six physical cores/twelve logical CPUs; Linux 6.18.49-1-MANJARO, GCC 16.2.1, Release `-O3 -DNDEBUG`, no LTO. CPU 0 governor: performance; frequency boost enabled.
- Benchmark timing enabled; debug tools, work probes and sanitizers disabled. Expensive per-step validation is a separate explicitly ineligible run set. Builds/tests finished before the timing capture.
- UTC capture: 2026-10-22T09:09:01Z through 2026-10-22T09:14:21Z. Load averages (1/5/15 minute) before: 4.23, 6.35, 5.92; after: 4.59, 5.18, 5.51. Host background activity/frequency/temperature are not fully controlled.
- Complete-step run-mean coefficients of variation range from 0.6% to 11.6% across the 36 cases. Use the recorded independent spread; one sample is insufficient to establish improvement.

```sh
cmake --preset cleanup-benchmark
cmake --build --preset cleanup-benchmark --parallel 4
python3 benchmark/capture_cleanup.py --mode validate --runner bin/cleanup-benchmark/platformator_benchmark_runner --output /tmp/new-cleanup-reference
# Finish other builds/tests before timing.
python3 benchmark/capture_cleanup.py --mode time --runner bin/cleanup-benchmark/platformator_benchmark_runner --output /tmp/new-cleanup-reference
```

Check out the recorded base commit, then overlay [source.tar.gz](source.tar.gz) to reproduce captured engine/CMake inputs and capture tooling. Bundled dependencies are from that commit except the archived/documented SDL_mixer WAV patch. The source fingerprint covers engine/public headers/CMake and that vendor patch; it is not a hash of all documentation or tests. Raw manifests record dependency versions and resolved geometry/parameters.

## Preserved evidence and verification

- [summary.csv](summary.csv): 36 aggregate rows; [capture.json](capture.json): all jobs, affinity, timestamps and host context; [invariants.json](invariants.json): all validation cases/tolerances.
- `runs/*.tar.gz`: 180 original summary CSVs, raw per-step CSVs, manifests and command/output logs. `validation/*.tar.gz`: all 36 instrumented captures with full final body/contact snapshots. Archives group by scenario/workers.
- [SHA256SUMS](SHA256SUMS) verifies archives, source and reports. All 180 raw runs were independently reparsed: required schema/metadata, 600 raw steps, unique repetition indexes 0–4, raw means and all 36 aggregates/spreads match. Extracted source fingerprint matches all timing manifests and invariant acceptance.
- [Validation evidence](../../../specs/001-cpp-engine-cleanup/validation.md) covers normal, ASan/UBSan, instrumented-TBB TSan, 1,000 scene cycles, work counters and source/installed/relocated SDK/Mario. LeakSanitizer remains blocked by ptrace on this host; this reference does not certify leaks or desktop audio races.

## Historical comparison limits

The [original pre-cleanup files](../../baselines/pre-cleanup-e01526b/RESULTS.md) remain unchanged. Their partial `legacy_frame` and outer `host_step` boundaries differ from this complete-step clock (which includes final cleanup/destruction and excludes recorder storage/outer-call overhead). Their physics has flagged support failures/worker nondeterminism; headless setup also included a camera that is now unnecessary. Do not infer a causal speed ratio from the tables.

The historical six-worker static-churn range (1.1205–1.1606 ms for its outer host step) is below the new complete-step range (1.3607–1.4376 ms). This was reviewed rather than hidden: current 1,000-churn work is affected-object creation/identity invalidation/reconciliation/free, with about 0.710 ms in final cleanup and no whole-scene repair or idle allocation fallback. The required lifetime/reference contract does more bookkeeping. These scopes/semantics do not provide an equivalent controlled regression measurement; future churn profiling should use this corrected baseline. Broad single-worker ranges overlap the historical reference. No universal speed-up claim is made.

Godot monitor timings remain separate measurements; no equivalent complete-step boundary is established. See [benchmark methodology](../../README.md).
