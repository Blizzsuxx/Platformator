# Final validation and dispositions

All requested implementation findings are addressed. The preserved input was the existing `001-cpp-engine-cleanup` checkout, including the user's staged `src/gamemanager.cpp` edits; its index version still matches the frozen input. Scope excludes P02/P03/P05/P06/P07, Python editor work and broader S06 refactoring.

- **S01**: Removed unused axis plumbing; `isPrimary`, endpoint/checkpoint handling, cell witness reduction and worker boundaries remain. Baseline/candidate physics and equal-endpoint/migration controls passed at one and six workers. See `evidence/us2-overlap-baseline.md` and `evidence/us2-validation.md`.
- **S02/P04**: Removed fallback selection, warning state, redundant checks and unused path-resolution parameters. All nonempty texture/audio/animation requests must succeed or throw; exact previously validated requests reuse retained resources. Alias mappings are runtime/type scoped and removed at resource eviction. Tests cover missing and malformed assets, diagnostics, multiple aliases, changed/deleted files and symlinks, eviction, nested scene/reference failures and runtime recreation; see `evidence/us1-validation.md`. Allocator/alias cost is documented in `evidence/work-memory.md`.
- **S03**: Removed the unused public collision-constructor penetration parameter; separation-based correction remains. Consumers must update calls and rebuild/relink; see `../../docs/CPP_API_MIGRATION.md`.
- **S04/S05**: `getCurrentClip()` is preferred and the old animation getter remains a forwarding alias; removed the unused internal behavior helper. Playing/stopped getter checks and consumer builds passed.
- **Narrow S06**: The two physics body/pair membership helpers share one internal primitive. Their predicates, owners and reconciliation boundaries remain; script-start tombstones are untouched. Baseline/candidate first/middle/last membership behavior passed at one/six workers.
- **P01**: Removed the empty destructor that prevented implicit moves. The controlled growth fixture observed 1,023 copied/replaced buffers in baseline and 1,023 preserved by candidate moves; real-renderer visual equivalence is unverified because X11 is unavailable.
- **E01/E02**: Removed unused Mario camera/enemy pointers, constants and player declarations. The serialized inert `cameraLead` field remains; baseline and candidate Mario smoke and load/save round-trip passed.

## Verification and limits

Final debug and production configurations each passed 24 tests with two instrumentation-only skips, including separately run package/installed/relocated consumer smoke; the benchmark label passed three tests. Final candidate ASan/UBSan passed five selected hosts with leak detection disabled. Both baseline and final candidate leak-enabled runs failed due to the host's LeakSanitizer ptrace restriction; leak safety is unverified. Baseline and final candidate TSan selections each had nine failures in SDL audio paths and one skip; first-error termination prevents excluding other races. Full logs are in `evidence/validation-logs.tar.xzdata` and `evidence/sanitizer-logs/diagnostics.tar.xzdata`.

[Performance analysis](evidence/performance-analysis.md) reports a substantial reduction in the isolated repeated-asset-request loop and a roughly 9.1% slowdown in an isolated 1,000-cycle load/release workload, plus measured live-alias memory overhead. The latter is a workload-specific tradeoff, not an engine-wide conclusion. Sixteen matched physics invariant snapshots were identical; short engine timing comparisons do not establish a general speedup or regression. Debug-on Release render timing and graphical output are unverified. The quickstart records the shorter 20/60-step captures and other deviations from illustrative commands.

`checklists/requirements.md` retains its two documented quality exceptions for the explicitly requested implementation-specific helper merge; implementation authorization was provided by invoking the implementation workflow. No reviewer-owned checklist markers were changed.

## Phase 7 convergence follow-up

The membership helper now publishes its index only after vector insertion succeeds. Resource-cache insertion and alias registration roll back transactionally on alias-allocation failure, preserving an existing resource. Alias-map buckets shrink geometrically below one-quarter occupancy after large peaks, retain at most a small table for ordinary churn, and treat shrink allocation failure as best-effort rather than allowing a resource destructor to throw. The migration guide consistently describes required-only asset loading and identifies historical fallback descriptions as historical. See `evidence/convergence/results.md` for direct failure-injection, retry, staged-scene and nested-texture tests.

The convergence debug and production selections each passed 26 tests with two instrumentation-only skips; seven targeted ASan/UBSan tests passed with leak detection disabled, and four targeted TSan tests passed. Leak-enabled checks remain unavailable in this host. A broader opt-in allocation-failure experiment also exposed termination in the bundled JSON library's allocating destruction path during animation loading; the targeted alias-publication tests do not prove recovery from every allocation failure in that dependency. Existing broader SDL audio TSan results and unavailable graphical comparison remain limitations.

Updated work and timing results are appended to `evidence/work-memory.md` and `evidence/performance-analysis.md`: the maps fell from 541 to 31 buckets after 299 of 300 aliases were removed; the final repeated-hit benefit remains large in the isolated probe, while a 300-alias eviction control measured additional geometric-rehash cost. The measured full load/release workload remains slower than the pre-feature baseline; no engine-wide speed claim follows.

## Phase 8 convergence follow-up

The user's updated JSON header and a small local fix for partially initialized containers now pass an exhaustive allocation-failure and retry sweep, including animation, discarded scene preparation and nested texture release, as well as 512-level nonallocating destruction. Filesystem-operation failures now retain the exact original request and underlying context, including through scene loading. See `evidence/convergence/results.md` for counts, sanitizer coverage, dependency provenance and remaining limitations. The earlier broad-failure paragraph records the historical result before this fix.

## Phase 9 convergence follow-up

`AssetReference` now prepares ownership changes transactionally when path copying fails, including cleanup of a newly loaded unowned resource. Long-path allocation-failure tests cover all three asset types, existing owners, staged and runtime resolution, retries and final eviction; debug and ASan/UBSan checks passed with leak detection disabled. See `evidence/convergence/results.md` for coverage and limits.
