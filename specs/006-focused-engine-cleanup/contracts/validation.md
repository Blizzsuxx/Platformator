# Validation and Evidence Contract

## Baseline and scope

Record HEAD, branch, status, tracked/staged patch and relevant untracked input hashes, toolchain, dependencies, host/affinity and build flags. Preserve the existing `src/gamemanager.cpp` edits. Freeze source including common pre-production fixture changes and all needed nonignored untracked inputs; verify a manifest and keep it and logs until archived. Do not label a dirty snapshot as a clean commit. Run baseline/candidate comparable controls; intentionally different fallback/error behavior receives separate explicit expectations.

## Correctness and safety

- Characterize asset paths for all three kinds and each runtime/component/scene/reference path, absent/unreadable/decode errors including nested frames, no-asset values, initial invalid paths and symlink escapes, aliases and canonical identity. Verify retained hits after file deletion/symlink replacement, eviction of all aliases after last release, revalidation after eviction, scene rollback, runtime teardown/recreation and no stale references. Count actual filesystem resolution calls outside timing; repeated same-request hit must do none.
- Check physics invariants with one and multiple workers, including endpoint ties, long intervals, chunk split/merge, fast motion, grid migration, overlapping multi-cell witnesses, insertion/removal, contact transitions and relevant lifecycle/retirement cases. Compare deterministic contract traces and declared physical tolerances; do not demand exact unconstrained solver trajectories. Test first/middle/last membership removals, redundant desired state and swapped index repair for both types.
- Exercise animation getters, collision public constructor migration, behavior lookup, scene compatibility, Mario smoke and public/relocated consumer builds. Compare debug tools on/off; capture equivalent overlays on a real renderer if possible and record unavailable verification explicitly.
- Run relevant ASan/UBSan and TSan baseline/candidate attempts including leak-enabled configuration. Archive diagnostics and separate environmental or dependency failures from introduced issues; leak-disabled runs do not establish leak safety. Existing prior SDL-audio TSan and LeakSanitizer environment reports are historical, not current passes.

## Work and timing

- Use identical common fixtures with retained references for representative repeated same and alias requests at small/large counts; include miss/load and eviction/churn controls. Measure calls, allocations and alias storage separately; verify no accidental asset release and no filesystem calls on a timed hit. Keep instrumentation out of Release timing.
- Compare debug-object insertion/growth for small and large populations, capturing copies/moves/allocations separately from runtime; use equivalent baseline/candidate overlay state and actual rendering where available. No claim for debug-off builds.
- Run relevant engine workloads with one and available multiple workers, static/dynamic and small/large broad-phase populations, dense and migration/churn controls; compare complete-step and relevant subsystem average and tail, never sum nested scopes. For any runtime claim use `benchmark/README.md`: equivalent Release binaries, recorded source/build and CPU affinity, same fixtures/seed/warmup/sample counts, at least five independent processes per cell, interleaved or shuffled order, retained raw samples/manifests and run variance. Investigate material regressions and workload-count differences; report incomplete captures and uncertainty.
- Do not claim a speedup or visual equivalence when measurement or graphical access is unavailable. Document the retained memory and eviction-time cost of alias bookkeeping, and any observed regression.
