# Performance analysis and regression investigation

## Comparable evidence

Final candidate: `b87bc7a61c539405ff6e77fc8f481a3fd2c6ad3510f2b089ca4c475dd27367db`; immutable current-engine baseline: `5ca76cf38d163c353254d4f74f286888be050a32f1c88b7215c25b44faff1031`. The final matrix has36 groups×5 independent processes×2stages=360 processes,120 warmup/600 measured,seed 0,dt 1/120 and matching affinity/compiler/flags/dependencies/physical parameters/boundaries. Jobs alternate baseline/candidate under recorded shuffle seed1. Strict schema 3 readers, every raw checksum and actual source fingerprints were verified. No deliberate competing builds/tests/probes ran during timing. Separate physical captures pass all36cross-stage and18 worker comparisons/stage.

Final raw data: `bin/physics-bookkeeping-20261025/captures-v2/`. See [all36means](performance-table.md), [means/tails/spread/worker CSV](performance-comparison.csv), [all scopes/run means](performance-data.json), and [analysis reproducer](analyze_results.py). Scope p95/p99 summaries are means of per-run quantiles; maxima in the CSV are worst observed run maxima. They are not a latency guarantee. Nested scopes are not added together.

## Concrete fix discovered during measurement

The initial candidate added an out-of-line sleep-island reset call for each awake body. The100-body/six-worker mean was+6.36%, then+5.22% in a fresh matched follow-up. The helper retains one definition but now lives in the private header so Release can inline it. The final object has no reset helper reference/symbol ([investigation](reset-helper-investigation.md), [symbol check](reset-helper-symbols.txt)). The corresponding final matrix delta is+1.41%. This is evidence that the avoidable call was removed; it does not isolate it as the only timing cause.

The initial360 process matrix,60 initial follow-up processes, source archive/patch and analysis remain intact at `captures/`, `followup-time/`, `candidate-v1-source.tar.gz`, [initial data](initial-performance-data.json) and [initial follow-up results](followup-results.json). Final builds, normal/package/work/sanitizers, memory observations and physical/timing captures were repeated after the fix. Old results are not relabeled as final.

## Final results and every higher mean

Final means are mixed. Dense narrow-phase population200 improves: one-worker30.939→29.354 ms (-5.12%), six-worker14.446→13.443 ms (-6.95%). The six-worker run-mean ranges are disjoint; the one-worker ranges overlap. The six-worker scaling ratio is approximately2.18× relative to one worker, compared with2.14× for baseline; this is not linear six-core scaling. Geometry math is preserved; these measurements do not isolate cache layout, compiler inlining, task balance or allocation behavior as the causal factor. No universal speedup is claimed.

All21higher complete-step means have overlapping observed run ranges. Overlap is descriptive, not a significance test or proof of equivalence. The following table retains every increase, including absolute size and tails; detailed minima/maxima/SD and individual means remain in the data. Largest leaf is descriptive among cleanup/broad/narrow/solver, not a causal attribution.

| Group | Mean change | Increase µs | Largest increasing leaf | p95 change | p99 change |
| --- | ---: | ---: | --- | ---: | ---: |
| add_and_remove-n10-r0-w1 | +2.48% | 0.8560 | cleanup | +2.1% | +2.5% |
| add_and_remove-n10-r0-w6 | +0.79% | 0.2046 | cleanup | +0.5% | +0.1% |
| broad_phase-n100-r0-w1 | +0.85% | 0.3582 | cleanup | +1.3% | +2.1% |
| broad_phase-n100-r0-w6 | +4.71% | 1.3724 | cleanup | +3.2% | +5.4% |
| broad_phase-n1000-r0-w6 | +1.49% | 2.8353 | cleanup | +1.0% | +3.2% |
| broad_phase_stress-n300-r0-w1 | +0.26% | 6.7983 | broad_phase | +0.6% | -7.3% |
| broad_phase_stress-n300-r0-w6 | +0.68% | 16.4339 | broad_phase | +1.4% | +1.5% |
| broad_phase_stress-n50-r0-w1 | +2.87% | 3.8475 | broad_phase | +8.8% | +8.7% |
| broad_phase_stress-n50-r0-w6 | +2.72% | 3.2153 | broad_phase | +6.4% | +5.2% |
| moving_add_and_remove-n1000-r0-w1 | +0.73% | 19.2005 | cleanup | +4.2% | -2.8% |
| moving_add_and_remove-n1000-r0-w6 | +1.88% | 24.7604 | cleanup | +5.1% | -0.2% |
| persistent_contacts-n10-r0-w1 | +10.54% | 0.0307 | cleanup | +2.4% | +11.2% |
| persistent_contacts-n10-r1-w1 | +0.03% | 0.0002 | cleanup | +0.1% | -13.1% |
| persistent_contacts-n10-r1-w6 | +0.02% | 0.0002 | cleanup | -8.4% | -8.6% |
| persistent_contacts-n10-r2-w1 | +0.51% | 0.0046 | narrow_phase | +6.8% | +6.1% |
| persistent_contacts-n10-r2-w6 | +7.00% | 0.0628 | cleanup | +24.0% | +12.9% |
| persistent_contacts-n1000-r0-w6 | +9.24% | 0.0268 | cleanup | +2.5% | +4.3% |
| persistent_contacts-n1000-r1-w1 | +10.18% | 3.9561 | narrow_phase | +15.0% | +16.2% |
| persistent_contacts-n1000-r1-w6 | +1.46% | 0.5596 | narrow_phase | -1.6% | +9.0% |
| rigid_body_container-n100-r0-w6 | +1.41% | 0.8706 | resolve_collisions | +6.9% | +4.5% |
| rigid_body_container-n1000-r0-w1 | +4.10% | 58.0945 | resolve_collisions | +6.3% | +5.6% |

## Focused final follow-ups

Six affected/larger positive groups were repeated with five fresh matched processes/stage, shuffle seed2, same inputs and flags. All60raw files/metadata/checksums were verified; original captures are untouched. Raw: `bin/physics-bookkeeping-20261025/followup-final/`; [run-level summary](final-followup-results.json).

| Group | Final matrix mean delta | Fresh follow-up mean delta |
| --- | ---: | ---: |
| broad_phase-n100-r0-w6 | +4.71% | +3.31% |
| broad_phase_stress-n50-r0-w1 | +2.87% | +0.82% |
| broad_phase_stress-n50-r0-w6 | +2.72% | +6.03% |
| moving_add_and_remove-n1000-r0-w6 | +1.88% | -1.37% |
| persistent_contacts-n1000-r1-w1 | +10.18% | -0.94% |
| rigid_body_container-n1000-r0-w1 | +4.10% | -1.43% |

The persistent1000/one-recipient/w1 increase (+10.18%) is driven by a candidate process mean61.64 µs while the other candidate means are35.98–41.21 µs. Baseline means are36.69–43.40 µs. No observation is removed from the primary mean. The fresh follow-up reverses to-0.94%. This fixture has1000 confirmed contacts but zero pending geometry, awake dynamic bodies or generated events during measurement; ordinary script dispatch remains outside the physics/cleanup scopes. The evidence does not establish a repeatable added per-contact physics cost.

The rigid-body1000/w1 increase (+4.10%) likewise includes one candidate process at1750.40 µs, while its other four means are1393.80–1419.96 µs, versus baseline1394.26–1443.60 µs. The fresh follow-up reverses to-1.43%. Retain both the high process and tails; no clean equivalence claim follows. Moving churn1000/w6 changes+1.88% initially and-1.37% in follow-up. Its largest increase is cleanup, whereas the underlying grid/lifecycle algorithms are unchanged. Queue/support membership still restores correctly and allocation/storage do not show growth.

The spatial controls require explicit caution. broad100/w6 remains+4.71% in the matrix and+3.31% in follow-up; stress50/w6 is+2.72% then+6.03%; stress50/w1 is+2.87% then+0.82%. Their measured candidate/pending/contact/awake-dynamic counters are all zero: they exercise existing collider/grid work, not the changed pair ledger or awake-body reset. Broad means are mainly cleanup; stress means track the unchanged broad-phase leaf. No changed hot bookkeeping traversal explains these deltas. Binary layout/compiler/cache effects and host scheduling are plausible but unproven; no profiling evidence assigns a cause. These remain measured downsides with uncertain repeatability/magnitude, not dismissed as noise or certified equivalent. Changing SAP, padding or scheduling constants to chase them without causal evidence would be speculative.

Remaining small-population add/remove and broad increases are mainly cleanup; larger add/remove improves in both worker configurations. Stress300 is+0.26%/+0.68%, alongside overlapping process ranges and the unchanged broad-phase algorithm. Moving1000/w1 is+0.73%, smaller than its prior+2.66%/+2.13% observations and cleanup-dominated. These costs remain reported, with no algorithmic or asymptotic regression detected.

Small persistent groups range from nearly zero positive differences to+0.031 µs (unobserved10/w1),+0.063 µs (two recipients10/w6) and+0.027 µs (unobserved1000/w6). Those percentages are highly sensitive to instrumentation/timer/host variability. One-recipient10 groups differ by less than0.001 µs. Scripted1000/w6 is+1.46%, with p95 lower; two-recipient1000/w6 improves2.10%. No unchanged-contact enumeration was introduced: independent work counters remain zero. Body100/w6 is+1.41% after the call fix; body1000/w6 improves0.88%. No additional state/caching/threshold change is justified by this pattern.

## Ownership and cost tradeoffs

Queue flag+index became index-only without a scan or synchronization layer. Four support pointer/flag fields became two exact contributor identities. Per-body signed deltas still batch net changes, preventing spurious wakes; old recipients survive until flush/free barriers. Reconciliation remains serial after pair workers, O(pending pairs) plus touched-body/neighbor work; deterministic dirty solver sorting and transition sorting retain their existing costs and locations. No scope work moved between phases in this feature. Shared-body solving/callbacks remain serial.

Geometry is an explicit pair/gravity function, with implementation-local exact shape/clipping/fallback helpers. Reachable operations are token-verified unchanged; unreachable circle branches from box-only clipping were deleted. Getter/interface and code-layout effects are not isolated by the benchmark; no algorithmic geometry speed claim is made. Body participation remains derived once from live current state with incremental indexes. The hot reset helper can inline while preserving a single source definition.

Pair layout is136→120 bytes and body size224 unchanged. Fixed-peak contact/support/queue churn and100-update idle probes pass at one/six workers, with zero warmed C++new/new[] allocations and no extra idle physical/event work. Process RSS samples overlap: no RSS improvement is claimed. See [affected work/memory](affected-work-memory.md). No known affected-path inefficiency is deliberately deferred under an undocumented exception; the discovered avoidable call was fixed. Measured but causally unresolved control deltas remain explicit limitations.

## Conclusion and limits

Technical evidence supports the state simplification and preserved physical/lifecycle/worker contracts. Performance is workload-dependent: the dense changed-pair workload improves in this sample, some controls regress, and host/compiler/layout causes are not isolated. This is one machine, five processes per primary group and focused follow-ups, not a statistical equivalence study or universal latency/scaling guarantee. Required observations, positive deltas, corrective work and uncertainty are retained. No overall performance-equivalence or speedup certification is asserted. Maintainer readability acceptance remains pending separately.

## Convergence follow-up — 2026-10-26

The [bounded perf/counter/control investigation](spatial-investigation.md) retains 192 additional processes and [machine-readable results](spatial-results.json). The six-worker spatial downsides persist in fresh unprofiled original-duration and extended-duration captures. Hardware/cache counters and whole-process sampled stacks do not establish their cause. No speculative engine rewrite was made. T044 is complete as investigation; T038 has been reopened and FR-011 / SC-005 performance acceptance remains unverified. Earlier measurements and uncertainty are preserved above; completion of measurements does not certify the outstanding acceptance condition.
