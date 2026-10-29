# T044 Resource Evidence

## Captures

`benchmark/artifacts/linear-render-culling/20261029-t044-run-01/resources/` contains 51 matching resource proofs: 17 bounded cases × pre-feature, candidate-disabled, and candidate-linear. Each proof uses seed 0, 120 warmup steps, 600 measured steps, and `dt=0.008333333333333333`. All proofs passed the strict work-counter, source/harness, renderer, and membership checks.

| Workload class | C++ `new`/`new[]` calls and requested bytes per 600 measured steps, each mode | Interpretation |
|---|---:|---|
| visible, sparse, offscreen, clustered, rotated | 0 calls / 0 bytes | No warmed C++ allocation observed at tested sizes/workers. |
| camera-pan and crossing | 600 / 19,200 bytes | Fixture trajectory allocation; equal across the three modes. |
| churn, 1,000 sprites | 48,600 / 3,369,192 bytes | Fixture replacement allocation; equal across modes. |
| headless crossing, 1 and 6 workers | 600 / 19,200 bytes | Fixture motion only; all render-work counters remain zero. |

Candidate-linear allocation calls/bytes exactly match candidate-disabled in every case. The candidate has no persistent visibility cache or culling-owned collection; code structure retains O(1) culling state as population grows. RSS is process-wide and cannot attribute its small before/after changes to culling. The retained JSON proofs contain per-case RSS values.

## Observer coverage and limits

The validation-only interceptor counts process-wide C++ `new` and `new[]` calls and requested bytes during each measured step, including allocations by oneTBB worker threads and any other thread active in the scope. Its atomic epoch and RAII close the observation on exceptional exit; overlapping scopes are unsupported. This observer is compiled only into work-probe validation builds. Separate Release timing probes have work probes disabled and contain no allocation observer/global replacement symbols.

The observer does not count aligned `new`, C allocation, SDL allocation, or graphics-driver allocation. RSS is process-wide, not an attribution method. These results bound only the tested fixture, renderer, sizes, and 600-step window; they do not establish total process allocation or hardware-renderer memory behavior. Headless work counters are exactly zero even though the common moving fixture allocates.

The independent proof audit is in `provenance/proof-verification.json`; resource checksums and mode comparisons are repeated by `scripts/audit_timing.py`. The regenerated run’s full file checksums are in `provenance/artifact-files.sha256`.
