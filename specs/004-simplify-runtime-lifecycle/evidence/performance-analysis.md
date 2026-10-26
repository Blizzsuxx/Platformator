# Performance and acceptance analysis

The final cleanup is accepted as a simpler correct design with comparable measured work/scaling under constitution I/IV. This does **not** claim a general engine speedup, zero possible latency regression, or exact timing equivalence. Every group retains raw timings, run variation, tails and an explicit disposition; unresolved precision remains unverified rather than converted into a numerical allowance.

## What changed and why it was rechecked

The first single-authority lookup used a checked Runtime::current helper route. A focused 15-process-per-variant measurement over 10M warmed out-of-line lookups (including the same volatile pointer store) measured means of 3.715 ns for frozen baseline, 4.885 ns for the first route, and 3.632 ns for the final direct checked route. These isolate the lookup boundary, not whole-engine speed. The extra route was removed while keeping one owner and the established unavailable-runtime error; full affected compatibility/work/safety and paired correctness/timing checks were repeated. Source and binaries for both candidates remain retained.

## Representative engine evidence

The final 840-process matrix covers six controlled modes plus uniform/dense, movement/churn, physical response and persistent contacts across small/large populations and one/multiple workers. Eighty groups have matching recorded counter means; four parallel rigid-body-container guards have differing pair/body/contact work because solver trajectories are intentionally order-unspecified. They pass independent physical/support validity, but their raw ratios are not presented as isolated optimization effects.

Initial and refined group means include both increases and decreases. The largest refined percentage increase is a sub-microsecond quiet scene; active examples also contain process-level outliers. No samples were removed or replaced. Identical-binary A/A controls produce apparent mean increases up to 5.57% across the selected quiet/contact groups despite identical code, demonstrating that percentage changes alone do not establish a source effect.

An exploratory paired process-mean 95% t interval (five pairs, unadjusted for 84 comparisons) identified one positive contrast: 1,000 persistent pairs, one scripted endpoint, two workers, initially +3.96%. A longer 20-pair capture reversed direction:

- Follow-up mean change: -2.87%; paired mean difference -1.1278 microseconds, exploratory 95% interval [-2.1261, -0.1295] microseconds.

This does not establish a persistent downside or a universal gain. All other final positive means lacked a positive exploratory paired interval; this is not an equivalence proof or an automatic acceptance threshold. The conclusion combines repeated paired evidence, same-binary control, the corrected lookup cost, source-level complexity/ownership review, unchanged controlled work and allocation, and preserved scaling. It does not follow merely from overlapping ranges.

## Worker scaling samples

Complete-step mean microseconds; values are workers 1 / 2 / 6. No new solver/group parallelism is claimed. Small or mostly idle populations can be dominated by scheduling/measurement costs; existing serial dependency boundaries remain.

| Controlled workload | Baseline us | Candidate us |
| --- | --- | --- |
| probe-mixed-n1000-r0 | 45.54 / 39.46 / 30.28 | 45.63 / 36.14 / 30.71 |
| probe-connected-n1000-r0 | 1181.05 / 1015.36 / 979.15 | 1159.73 / 1010.36 / 968.22 |
| probe-hierarchy-n1000-r0 | 424.33 / 250.57 / 142.42 | 442.65 / 255.43 / 146.80 |
| probe-lifecycle-n1000-r0 | 410.71 / 360.55 / 318.81 | 411.80 / 357.80 / 327.23 |

## Allocation, storage and limitations

Final baseline/candidate work runs at 1/2/6 workers preserve zero counted warmed-idle C++ allocations and unrelated work, affected-subtree-only publication, terminal cancellation, physical/script separation and fixed-peak event/contact capacity reuse. No new runtime state/cache/queue/framework was introduced; two unused request fields, duplicate active-manager storage, dead methods and duplicate frame/timer maintenance were removed. No memory-size or all-system allocation improvement is inferred from source field counts.

Enabled ASan/UBSan and TSan findings are separately documented: existing FluidSynth leaks and SDL/libdbus lock-order reports reproduce on fully instrumented frozen source. They remain unresolved, not clean safety passes. No data race was reported in the outside-sandbox selected TSan run; sandbox-only reports and detector limitations remain visible.

Full per-group means/median/p95/p99/max, independent-run standard deviations, paired exploratory contrasts, counter differences and dispositions are in per-group-dispositions.json. Exact latency equivalence, untested workloads/platforms and universal race/deadlock/leak freedom remain unverified. Actual maintainer readability acceptance is still pending.
