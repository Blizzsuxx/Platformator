# Runtime cleanup validation

Technical work is complete: **40 of 41 tasks complete; the maintainer explicitly deferred final readability review (T041) to a separate prompt**. Preliminary feedback: "it looks somewhat better"; this is not acceptance of all six audit paths. No code change is inferred to be human-readable from tests. Existing sanitizer findings remain unresolved as detailed below.

## Result

- Removed duplicated rendered-frame control flow, two dead private methods, two unused scene-request fields and the separate GameManager active-instance storage/accessors/publication.
- Kept one checked Runtime authority and explicit shutdown before reset. The final direct lookup preserves the baseline fast path; an initially slower helper route was measured and corrected.
- Shared the transition-only sleep timer reset, removed the unused impact argument/stale scaffolding, named root lookup clearly, and applied braces plus logical spacing. Group scratch/two passes, support batching, incremental participation, phase restoration and callback-before-free barriers remain.
- Public include/platformator headers and serialized formats are unchanged. No unrelated subsystem rewrite or new runtime framework/state/queue was introduced.

## Executed checks

| Check | Actual outcome |
| --- | --- |
| Full ordinary debug CTest | 26 registrations: 24 pass, 2 expected instrumentation-only skips; package/public-consumer passes |
| Production build | Engine, runner, public-header checks and Mario consumer build |
| Work/allocation | Baseline/candidate workers 1/2/6 pass; zero counted warmed idle allocation/unrelated work, affected subtree visits and fixed-peak capacity reuse preserved |
| Release contracts | Benchmark-label checks pass |
| Final paired correctness | 84 groups × two stages = 168 processes pass |
| Final timing matrix | 84 groups × five processes × two stages = 840 captures retained |
| Timing investigation | Initial 840-run candidate capture; 480 A/A controls; 240 longer paired contact follow-ups; 45 lookup micro processes; explicit per-group dispositions |
| Enabled outside-sandbox ASan/UBSan | 11 hosts: 3 pass, 8 report existing FluidSynth initialization leaks; no access/UB diagnostic observed; matching leak totals reproduced with current tests on fully instrumented frozen core |
| Outside-sandbox TSan | 16 registrations: 13 pass, 2 existing SDL/libdbus lock-order failures, 1 instrumentation skip; no data-race warning in that run; the same two warnings reproduce on frozen core |
| Style / scope / ownership | Braces and stage spacing reviewed; formatting/diff checks pass; public API, lifetime and worker boundaries retained |

Sandbox LeakSanitizer inspection failure and sandbox-only SDL race reports are retained separately; they are not treated as valid clean safety checks. No detector suppression was introduced. Unresolved leaks/lock-order warnings are not marked passed and are not fixed by this cleanup.

## Performance disposition

[Performance analysis](evidence/performance-analysis.md) records all increases, decreases, work differences, tails, run variation and follow-up findings. The final lookup's isolated cost is comparable to baseline; the first helper route's overhead was removed. The only positive exploratory paired process-mean contrast in the final matrix changed direction in a longer 20-pair check. Four parallel physical guards have differing solved work, so their raw ratios do not isolate implementation overhead.

The simpler design is accepted under the constitution based on the combined behavioral, work/allocation, lookup, repeated paired/control and scaling evidence. This is not a general speedup, exact latency equivalence, universal no-regression guarantee, or leak/race/deadlock-freedom claim. No arbitrary percentage allowance is used.

## Traceability and retained artifacts

FR-001–FR-004 / SC-001–SC-002 / SC-004: [inventory](inventory.md), source review and [maintainer review](maintainer-review.md), which remains pending. FR-005/J1–J3: [US1 characterization](evidence/us1-characterization.md), [results](evidence/us1-results.md), [walkthrough](audit-us1.md). FR-006–FR-007/J4–J5: [US2 characterization](evidence/us2-characterization.md), [results](evidence/us2-results.md), [walkthrough](audit-us2.md). FR-008/J6: [US3 characterization](evidence/us3-characterization.md), [results](evidence/us3-results.md), [walkthrough](audit-us3.md). FR-009–FR-011 / SC-003 / SC-005–SC-006: [compatibility](evidence/final-compatibility.md), [work/memory](evidence/final-work-memory.md), [ASan/UBSan](evidence/asan-ubsan.md), [TSan](evidence/tsan.md), [paired matrix](evidence/final-matrix-validation.md), [timings](evidence/final-timings.md), [analysis](evidence/performance-analysis.md). FR-012: prepared six walkthroughs and pending actual maintainer findings.

Raw root: `/home/user/Projects/Platformator/bin/runtime-cleanup-validation`. [Capture locations and reproducibility](evidence/capture-locations.md) identify source/binary/fixture hashes, origin commit, common validation patch, raw checksums and durable per-group summaries. Source snapshots and earlier attempts are preserved; no historical baseline was overwritten or presented as current evidence.

The requirements checklist remains byte-identical and read-only. No extensions.yml exists, so before/after implementation hooks are skipped according to the skill. No branch switch, commit or publication was performed.
