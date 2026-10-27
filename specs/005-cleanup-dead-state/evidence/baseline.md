# Baseline snapshot

Source snapshot: `/tmp/platformator-dead-state-0j46vn51/baseline-source`; source manifest `/tmp/platformator-dead-state-0j46vn51/baseline-source-sha256.json`, 8663 files, canonical manifest SHA-256 `41320610d321efce617fbe00360d4b51e3df4f4344ebbd7719c1a8e6cb0cb887`. Snapshot created from tracked and nonignored untracked inputs including the common test fixtures; each copied file matched the workspace before production changes. Git metadata is absent; refer to provenance.md for actual revision and initial status.

Characterization: collider cleanup, runtime foundation and asset root tests passed (3/3). The common pair-churn fixture passed validation at 10 pairs/1 worker and 100 pairs/6 workers, checking candidate/contact populations and exact Enter/Exit transitions each step.

The common probe was refined before production changes to check final population outside timed samples and to refuse existing sidecars; the identical updated fixture was copied into the snapshot and the manifest updated. Re-run its validation before use.

- `baseline-debug-ctest.log`: 26 registered, 24 passed, 2 instrumentation-only skipped; package smoke passed.

- `baseline-production-ctest.log`: 26 registered, 24 passed, 2 instrumentation-only skipped; package smoke passed.

## Reconstruction after temporary-directory loss

The original `/tmp/platformator-dead-state-0j46vn51` directory and its baseline build and unarchived logs were removed between turns. The baseline for remaining timing runs is being reconstructed in `bin/dead-state-baseline-source` from recorded HEAD `66707f6` and the same shared CMake/test/probe fixtures. This reconstruction is not asserted to match the lost full-tree manifest; its workload equivalence must be checked against the archived original invariants before timing comparisons.

The reconstructed baseline completed the full 18-job invariant matrix. All controlled snapshots and four 600-step pair-churn traces match the archived original baseline; both uncontrolled six-worker rigid-body populations differed in support totals under the existing waiver, with all runs independently valid. See `invariants/reconstruction-comparison.txt` and `invariants/reconstructed-baseline.tar.xzdata`. This supports workload equivalence without claiming the lost complete source manifest was reproduced.
