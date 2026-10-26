# Spatial-control regression investigation — 2026-10-26

**Disposition: investigation completed; performance acceptance remains unverified.** Repeated six-worker downsides remain. No specific introduced code cost has been established, so no speculative engine change was made. T044 records the bounded investigation; T038 is reopened because its no-unexplained-regressions condition is not met. Human review T043/T045 remains separately pending.

## Evidence and controls

The unmodified preserved baseline and final candidate runners retain source fingerprints `5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031` and `b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db`. Both use matching Release flags, dependencies, schema 3, seed 0, dt 1/120, 120 warmup steps and CPU masks 0 / 0,2,4,6,8,10. The governor is performance. No builds/tests or deliberate competing jobs ran alongside captures; other host activity and hardware effects are not controlled away. Runners and libraries were not rebuilt or replaced.

The user installed the distribution `perf` package (7.2.3-1); hardware counters and DWARF call-stack recording work without escalation. Profiling uses `cycles:u`, 499 Hz, 8192-byte DWARF stacks, one process per stage/group over 60,000 measured steps. Separate hardware-counter and cache-counter controls use five matched alternating processes per stage/group over 6,000 steps. All requested counters report 100% running time in the retained captures. An exploratory six-event probe multiplexed conflicting events and was excluded; the retained four-event cache set did not multiplex.

Unprofiled timings use ten independent processes per stage/group for each of 600 and 6,000 measured steps, shuffled with seed 26 and alternating stage order. The 600-step runs reproduce the original duration; 6,000 and 60,000-step runs extend the trajectories and are labeled separate controls, not replacements for the original workload. Boundary-only recorder manifests cannot detect external perf instrumentation: profile/stat/cache provenance explicitly marks those runs ineligible for performance comparisons. Whole-process perf counts/samples include startup, recorder/output and teardown, plus oneTBB workers; they are not complete-step-only counts or a call-frequency census. Only unprofiled `time/` runs below support timing comparisons.

Raw evidence: `bin/physics-bookkeeping-20261026/` with `profile/`, `stat/`, `cache/`, `validate/`, `time/`, per-phase schedules/commands, CSV/samples/manifests/logs/checksums, perf data and reports. [Machine-readable results](spatial-results.json), [capture reproducer](profile_spatial.py), [analysis reproducer](analyze_spatial.py). Retain this ignored raw tree before cleaning builds.

## Fresh unprofiled complete-step timings

Times are microseconds. p95/p99 columns are means of per-process quantiles; worst is the largest single-step maximum across processes. All process means, medians, tails, spread, paired differences and eight scopes are retained in the JSON; no sample or high process is discarded.

| Group / steps | Baseline mean | Candidate mean | Change | Baseline / candidate p95 | Baseline / candidate p99 | Baseline / candidate worst |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broad_phase-n100-w6-steps600 | 30.967 | 31.415 | +1.45% | 39.908 / 40.165 | 46.943 / 46.652 | 82.778 / 84.881 |
| broad_phase-n100-w6-steps6000 | 31.512 | 32.517 | +3.19% | 40.628 / 41.550 | 46.515 / 47.912 | 1440.936 / 2871.902 |
| broad_phase_stress-n50-w1-steps600 | 141.945 | 139.258 | -1.89% | 184.489 / 178.585 | 197.509 / 198.254 | 545.707 / 549.704 |
| broad_phase_stress-n50-w1-steps6000 | 138.948 | 139.149 | +0.14% | 179.858 / 180.873 | 198.551 / 199.598 | 617.042 / 616.511 |
| broad_phase_stress-n50-w6-steps600 | 121.730 | 127.383 | +4.64% | 152.779 / 157.832 | 161.876 / 171.526 | 178.829 / 798.626 |
| broad_phase_stress-n50-w6-steps6000 | 123.392 | 126.909 | +2.85% | 155.286 / 159.997 | 165.328 / 170.088 | 3206.928 / 700.820 |

Broad100/w6 retains a downside at both lengths; all ten paired 6,000-step means are higher (+1.07% to +5.95%). Stress50/w6 also retains a higher overall mean, although individual paired differences cross zero. Stress50/w1 is -1.89% at 600 steps and +0.14% at 6,000. Thus the six-worker downside is not explained away by a short capture or a single high process. Overlapping run ranges do not establish equivalence, and these comparisons are not an equivalence study.

Per-child user/system time and voluntary/involuntary context-switch counts are retained for unprofiled runs. Preemption counts vary substantially, particularly with six workers; no causal link between an individual preemption count and the measured slowdown is established. Affinity pins the allowed CPU set; it does not isolate those CPUs from the OS.

## Profiles and hardware counters

Both broad100/w6 profiles show the same major families: GridCell operation flushing, Grid lookup, concurrent-vector operations, and oneTBB dispatch/wait/steal. The dispatcher process symbol accounts for 36.63% / 36.10% of sampled user cycles (baseline/candidate). Both stress50/w6 profiles show the same AABB/projection/segmented-list families with dispatcher shares 28.65% / 27.90%. With one worker, AABB axis-overlap checking is the largest self symbol (19.14% / 18.20%). All six profiles report zero lost samples. These are relative whole-process self-sample shares, not elapsed-time shares; a lower scheduler percentage is not proof of less scheduling cost. Full child call chains and self reports are retained.

The sampled AABB, GridCell, segmented-interval-list and local-sort implementation files have identical source hashes in both trees (`hot-source-identities.json`). The controls have zero candidate pairs, pending geometry pairs, live contacts, awake dynamic bodies and generated Enter/Exit records throughout the unprofiled captures. They do perform kinematic movement and collider/grid synchronization. There is no sampled new support-ledger traversal or awake-body reset work to remove from these controls. Identical source and zero counters do not prove identical compiled layout/cache behavior or performance.

| 6,000-step whole-process counter control | User cycles | Instructions | Task CPU time | Branch misses |
| --- | ---: | ---: | ---: | ---: |
| broad_phase-n100-w6-steps6000 | +1.17% | +0.37% | +1.00% | +0.25% |
| broad_phase_stress-n50-w1-steps6000 | -2.23% | -0.01% | -2.45% | -10.71% |
| broad_phase_stress-n50-w6-steps6000 | +2.40% | +0.45% | +2.83% | +2.95% |

| Separate 6,000-step cache control | Instructions | L1 instruction-load misses | L1 data-load misses | Generic cache misses |
| --- | ---: | ---: | ---: | ---: |
| broad_phase-n100-w6-steps6000 | +1.33% | -5.00% | +3.51% | +4.49% |
| broad_phase_stress-n50-w1-steps6000 | -0.01% | -22.55% | +5.24% | -4.96% |
| broad_phase_stress-n50-w6-steps6000 | +0.12% | -4.79% | +2.66% | +4.17% |

The ordinary counter set has less than 0.5% instruction change in six-worker groups; the independent cache set has +1.33% / +0.12%. One-worker instruction counts are virtually identical in both sets. This does not support a substantial new algorithmic traversal; worker spinning and scheduling themselves contribute instructions. The cache set shows modestly higher data/generic cache misses in six-worker candidate runs and fewer instruction misses. This is consistent with a cache/scheduling sensitivity but does not isolate an introduced cause, quantify its contribution or establish a justified tradeoff. Generic cache event meaning is hardware-specific; no DRAM-latency or cache-level speed claim follows. User-only context-switch/migration perf events reported zero and are not used as evidence of no switching; separate unprofiled child rusage records are used to observe preemption instead.

## Correctness, provenance and limitations

Six fresh 6,000-step per-step-oracle captures pass, with all three baseline/candidate exact/tolerant physical comparisons and both stress one/six-worker comparisons. Strict readers verify every capture and matching workload/build/hardware/dependency/boundary metadata. The analyzer verifies all per-phase raw checksums: 6 profiles, 30 ordinary counter runs, 30 cache runs, 6 validation runs and 120 unprofiled timing runs, 192 processes total. Existing final full/package/work/sanitizer/36-group physical checks remain applicable because no runtime, public header, build configuration or test fixture changed. This investigation does not convert the known FluidSynth leaks or SDL/libdbus lock-order warnings into passes.

The first profiling harness attempt incorrectly applied the snapshot comparator to boundary-only summaries and stopped after two captures. It was corrected to compare boundary summaries for timing/profiles and use the snapshot comparator only for validation. Those two valid raw profiles were reused without overwriting them; the original failure/resume logs remain. An analysis retry also preserves the original compact reports instead of requiring perf report text rerendering to be byte-identical. Neither correction changed runner inputs, raw captures or engine code. A fallback sampler was compiled before perf became available, never used, and removed.

## Acceptance disposition

- T044: complete as a bounded profiling/control investigation with a recorded inconclusive causal disposition. This is not a successful optimization or performance-neutrality claim.
- T037: investigation work was executed, including the earlier fixed extra reset call; its completion does not certify acceptance of unresolved controls.
- T038: **pending**. Its explicit no-unexplained-regressions checkpoint cannot pass while the repeatable six-worker downsides lack a supported cause/tradeoff. FR-011 / SC-005 performance acceptance remains unverified; the comparison and reporting portions of FR-010 / SC-005 are present.
- T043/T045: pending actual maintainer walkthrough; no review result is inferred.

No new known inefficient implementation was identified for a feasible code fix, so no speculative SAP rewrite, scheduling threshold, cache, padding or `EFFICIENCY:` exception was added. Remaining acceptance needs evidence supporting the downside disposition or a demonstrated scoped fix followed by affected validation/recapture. The readable state reductions and dense-contact improvements do not waive this requirement.
