# Focused incremental-render pilot

> **Artifact availability, 2026-10-30:** The user deleted the generated benchmark bundles to reclaim disk space. The findings below are historical; linked raw captures, checksums, binaries and verifier/replay scripts are no longer available locally. Do not automatically regenerate the former campaign. See [the compact workflow](../quickstart.md).


## Scope and identity

This is an exploratory screen, not a feature acceptance result. It compares current-linear, cached-linear, and indexed modes on six selected workloads using three shuffled independent processes per strategy/case, five warmup steps, and 30 measured steps. Timing keeps the existing independent `render` and `complete_step` scopes and also records paired host step-plus-render samples. Each process retains every raw step/render/paired row.

| Identity | Value |
|---|---|
| Candidate source SHA-256 | `5a500fe96e6cb99acc9af6e81c60f61710380c0e1d14362e84d602d9bb53dcf8` |
| Fixture SHA-256 | `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6` |
| Harness SHA-256 | `bd85667c67f3a7b0743de854bc248cb1d1f64e6c0beb0d94d360d9898d2976dd` |
| Asset tree SHA-256 | `f2a813955fb1a9ac270b18c88a722c0090cd9aecaedd65108641240605c6be02` |
| Timing / validation build | Release, sanitizer none, debug tools off; work probes off/on respectively |
| Host | AMD Ryzen 5 7535HS, 6 physical / 12 logical CPUs, Linux 6.18.55-1-Manjaro |
| Affinity | CPUs 0–11 available; the pilot was not pinned |
| Renderer | SDL software, 320×240, scale 1, full viewport, VSync 0, mapping eligible |

The final report, raw index, report-linked resource proofs, binaries, build identities, source-input manifests, capture driver, commands, logs, and checksums are under [`benchmark/artifacts/linear-render-culling/009-20261029-run-01/`](../../../benchmark/artifacts/linear-render-culling/009-20261029-run-01/). The relative-path index is [`pilot-final-index.json`](../../../benchmark/artifacts/linear-render-culling/009-20261029-run-01/pilot-final-index.json); `scripts/verify_pilot.sh` checks the report, raw samples, resource proofs, source/build identities, and probe binaries. That focused subset also passed the same verifier from a temporary relocated copy.

## Process-mean results

Values are the mean of three independent process means, in milliseconds. `paired` is the directly timed complete step-plus-render interval; it is not calculated by adding the separately measured scopes.

| Case | Workers | Render current / cached / indexed | Complete step current / cached / indexed | Paired current / cached / indexed |
|---|---:|---:|---:|---:|
| Mostly offscreen, 10k, 0% changed | 1 | 1.392 / 0.996 / 0.968 | 0.00091 / 0.00067 / 0.00065 | 1.395 / 0.998 / 0.970 |
| Camera motion, 10k, 0% sprites changed | 1 | 0.476 / 0.107 / 0.084 | 0.00074 / 0.00060 / 0.00063 | 0.477 / 0.108 / 0.086 |
| Small moving subset, 10k, 1% changed | 1 | 0.486 / 0.076 / 0.071 | 0.02124 / 0.02243 / 0.01978 | 0.509 / 0.100 / 0.092 |
| Fully moving crossing, 10k, 100% changed | 1 | 3.149 / 3.820 / 4.359 | 0.72310 / 0.92232 / 0.97075 | 3.875 / 4.748 / 5.336 |
| All visible, 1k, 0% changed | 1 | 1.020 / 0.979 / 0.966 | 0.00078 / 0.00063 / 0.00055 | 1.023 / 0.980 / 0.968 |
| Fully moving crossing, 10k, 100% changed | 6 | 3.324 / 4.027 / 4.252 | 0.77746 / 1.05407 / 0.99126 | 4.108 / 5.088 / 5.250 |

Paired process differences retain run-index order. Cached-linear minus current-linear was `[-0.394, -0.437, -0.359] ms` for mostly-offscreen, `[-0.373, -0.363, -0.370] ms` for camera motion, and `[-0.411, -0.415, -0.402] ms` for the 1% moving subset. It was `+0.941, +0.771, +0.905 ms` for single-worker crossing and `+1.611, +0.704, +0.626 ms` for six-worker crossing.

Indexed minus cached-linear paired differences were `[-0.053, -0.020, -0.011] ms` for mostly-offscreen, `[-0.019, -0.029, -0.021] ms` for camera motion, and `[-0.013, -0.001, -0.010] ms` for the moving subset. They were `[+0.931, -0.077, +0.912] ms` for single-worker crossing, `[-0.263, +0.238, +0.511] ms` for six-worker crossing, and `[-0.038, -0.007, +0.007] ms` for all-visible. Full per-process render/step/paired means, per-process p95/p99/worst frame values, and paired differences are in `captures/pilot-final/report.json`; raw samples are checksummed in `raw/raw-index.json`.

The short run suggests the cache reduces total cost in mostly-offscreen, camera-pan, and small-subset cases. The index shows small additional paired-time reductions in the sparse and pan cases, but the changes are only 11–53 microseconds and three processes are not enough to establish them. The fully moving cases show a large cache/index regression; the all-visible differences are small and mixed. Do not promote a universal winner from this pilot.

## Resource observations

All four modes used the same resolved workload, SDL mapping, source, fixture, and harness for each case. Measured-step allocation calls were zero in static cases for all modes; dynamic cases recorded 30 process-wide C++ `new`/`new[]` calls per 30 steps in every mode. The observer cannot attribute aligned new, C, SDL, renderer-driver, or OS allocations. The candidate strategies did not add measured allocation calls or bytes over disabled mode in these captures.

The retained culling-owned byte totals are lower bounds. The pilot's work-probe observer omitted the capacities of the cached-visible-rank merge and change buffers. At record capacity R=16,384, these two buffers account for about 384 KiB on this build; the historical `owned_bytes` and `peak_owned_bytes` values do not include it. The focused regression for the corrected observer passed, but the original report and raw proofs remain unchanged and must not be used as complete retained-memory accounting or as SC-005 evidence. See [resources.md](resources.md) for the correction and its evidence limits.

Camera motion refreshed zero bounds for cached/indexed; indexed queried once per measured render (30 queries). The 1% subset refreshed 3,000 records over 30 steps. Fully moving crossing refreshed 300,000 records and performed 91,341 units of visible-order work in cached/indexed. The raw proof records setup/registration, first-warmup-step, record/cell capacities, growth, reset time/allocations, and before/after/reset RSS. For example, on the 10k static case, indexed setup/registration took 24.11 ms, its first warmup step 2.07 ms, the recorded capacity-growth observation 16.99 ms, and reset 6.41 ms; cached-linear measured 15.79 ms, 1.61 ms, 8.48 ms, and 4.34 ms respectively. These are cold/resource observations, not warmed timing rows.

## Decision

SC-006 remains open. This pilot has too few independent processes and too few cases for acceptance, has no selected-case framewise parity proofs or frozen-reference controls, and includes a material fully dynamic regression. Expand only to answer the stated uncertainty: confirm the sparse/pan index reductions with more independent runs and larger spread populations, then assess the crossing regression and cold/memory tradeoff before changing the accepted production strategy.
