# Research: Clean Up Dead State and Internal Code

Research baseline: Git `66707f6`, actual branch `001-cpp-engine-cleanup`, plus the feature specification in the working tree. This is source/design research, not execution evidence for the future cleanup. Recheck the exact implementation, user changes and public callers before edits.

## Decision 1: Remove C01 slot infrastructure; keep live pair ownership/indexes

**Decision**: Remove `Grid::pairSlots`, `freePairSlots`, `ColliderPair::pairSlot`, and their allocation/recycling writes when the fresh audit confirms no consumer. Retain `candidateCollisions`, canonical handles, net witness counting, adjacency and its indices, the pending queue/index, pair-local result/manifold state, and contact/solver memberships and indices.

**Rationale**: Current insertion/removal maintains the slot table but neither workers nor any other production/known test consumer reads its stored pointer. Workers receive pair pointers from a fixed queue; pair-owned state is reduced after worker completion. The original design proposed slot-indexed results; the current code chose pair-local results. The later `specs/003-simplify-physics-bookkeeping/data-model.md` identifies nonexistent enumeration consumers; annotate its historical claim with a supersession note and update current documentation, while preserving evidence snapshots. Table storage and churn writes have a plausible cost but no measured improvement is established.

**Alternatives considered**: Keep unused slots as speculative future infrastructure (unnecessary retained state); connect a new array of results or enumeration users (changes architecture without a demonstrated need); remove other pair indexes because they resemble the dead index (incorrect: those indexes are consumed for queue/list maintenance).

## Decision 2: Preserve the solver's actual bias and warm start, remove C03 only

**Decision**: Remove `ClipPointWithData::accumulatedNormalImpulseBias`, its initialization/copy and the tests that assert preservation of arbitrary values for that field. Keep feature matching, normal/tangent warm-start impulses, `bias`, pre-step computation and scalar/block solver equations unchanged.

**Rationale**: The field is never read by the solver. Penetration correction and restitution are combined in `bias`, applied through the normal impulse, and used in both paths. [Box2D-Lite's contact structure](https://github.com/erincatto/box2d-lite/blob/master/include/box2d-lite/Arbiter.h) and [solver](https://github.com/erincatto/box2d-lite/blob/master/src/Arbiter.cpp) likewise declare/copy `Pnb` but do not use it in their active impulse calculation; this supports a likely origin but does not prove copying. Public struct layout and field removal require migration and rebuild guidance.

**Alternatives considered**: Implement separate split/bias impulses (new solver behavior and out of scope); delete all bias calculations (breaks penetration/restitution response); remove the entire remapping test (would lose meaningful normal/tangent warm-start coverage).

## Decision 3: Remove only transitive dead physics helpers (C09–C12)

**Decision**: Remove private support accessor declarations/definitions, `finishColliderSync`, unused pair endpoint/single-step witness methods, and the dead binding/rebinding chain. Retain support state and contribution credits, `queuePairsForCollider`, batched `addToWitnessCount`, binding constructors/projection caching and chunk repair. Public `BoundingRadiusProjectionAxisBinding::bind` and `BoundingRadiusProjectionProxy::setBoundingProjection` removals require migration and rebuild guidance.

**Rationale**: These helper chains have no current engine consumer; their downstream dependencies do have active callers. `Grid::flushPendingCellUpdates` must continue to complete parallel cell work, reduce deltas, retire empty cells, queue affected pairs, refresh responses and reset dirty/index state in the existing order.

**Alternatives considered**: Remove shared dependencies with the dead wrapper (incorrect); retain public methods unconditionally (conflicts with specified C12 cleanup; retain only if the re-audit establishes a required supported contract and record the evidence); redesign the broad phase (not needed).

## Decision 4: Preserve subdivision and temporal coherence

**Decision**: Make localized bookkeeping removals without changing sweep-and-prune ordering, sorting strategy, cell scheduling, pair-delta reduction, event ordering, or solver parallelism.

**Rationale**: [The local paper](../../paper/paper.txt), §§3.1–3.3, explains cell subdivision with duplicate witnesses, segmented endpoint lists/checkpoints including fast-movement cancellation and split/merge, and overlap-change output. Current Grid aggregates and orders per-cell deltas by canonical identity and publishes/removes pairs on first/final witnesses. These invariants still apply even though the separate pair slot table does not. The paper's tuning numbers and historical runtime measurements are not contemporary performance evidence.

**Alternatives considered**: Global sorting, batch merge, adaptive switching or a new parallel reduction may be worthwhile in another feature but are not necessary for deleting this state and would require independent correctness and comparative measurements. Re-enumerating all pairs each step would discard temporal coherence.

## Decision 5: Remove C02 and C04–C08 without changing lifecycle

**Decision**: Remove unused hierarchy depth, Audio index, manager-only dropped-step aggregate, unused GameObject constructor, GameManager add wrapper and unreachable component-removal notification chain. Keep hierarchy order/sorting, clock drop state and its tests, scene publication, reconcile/retire ordering, track ownership and transient-audio records.

**Rationale**: Current code assigns depth without reading it; registered Audio has no manager-owned indexed collection; `droppedSimulationSteps` is unobserved; live registration/removal uses other paths. `SimulationClock::droppedSteps()` and its state remain observed by existing clock tests, so C05 does not justify removing them. Most importantly, C02 removes a private member of the installed `GameObject` class. `getComponent<T>()` is header-defined and accesses later fields, so class layout changes can break stale consumer objects even without a removed public method. Document C02 rebuild/ABI impact as well as C04's public accessor and layout break.

**Alternatives considered**: Add audio list/telemetry to use dead fields (unrequested behavior and overhead); keep the whole deleted-helper chain because live removal exists elsewhere (different path); preserve binary layout with a padding member (keeps dead per-object state and conflicts with cleanup goal); remove `SimulationClock` drop accounting (changes tested policy).

## Decision 6: Remove C13/C14 while preserving diagnostics and active debug output

**Decision**: Remove the `ResolvedAssetPath::requestedPath` copy and corresponding aggregate elements, leaving local `raw` available to validation/errors/fallback warning keys. Remove the six uncalled direct debug rendering functions and their private declarations, retaining the `add*DebugObject` buffered overlay path and controls.

**Rationale**: No resolved-path consumer reads the copied string. Direct renderer methods have no active callers; buffered render is the active implementation. Existing source tests exercise asset rules, but no automated pixel-level overlay equivalence was identified; verify both build configurations and a graphical/capture comparison when feasible and report if unavailable.

**Alternatives considered**: Cache paths or change loading order (P04, outside scope); remove debug name/label/copies or SDL_ttf (P01/P06, outside scope); rewrite active rendering (unnecessary behavior risk).

## Decision 7: Compatibility and current design notes

**Decision**: Deliberately remove only public declarations/layout specified by C03, C04 and C12, and account for the private C02 field changing the layout of the installed GameObject class. Update `docs/CPP_API_MIGRATION.md`, any current affected public-facing documentation, and independent consumer coverage. Require a full rebuild of library and consumers; do not claim ABI compatibility. Preserve scene/asset formats and all remaining public behavior.

**Rationale**: The user asked to remove the audited state, and source/public header inspection demonstrates which consumers must migrate. C02's ABI exposure was discovered during planning and is reflected in the specification's FR-009 and assumptions. Unobserved repository usage cannot establish absence of external users.

**Alternatives considered**: Silent removal (misleading); treat all public methods as safe merely because no in-repository caller exists (unsupported); maintain padded/legacy state indefinitely (does not meet requested cleanup).

## Decision 8: Evidence and validation strategy

**Decision**: Record a fresh C01–C14 disposition, characterize behavior before edits, and use matched baseline/candidate sources and common fixtures. Run applicable CTest, independent consumer, debug-on/off, work and safety checks. Compare one/multiple workers and pair churn, multicell witnesses, dense/persistent and moving workloads. Separately record structure/capacity and allocation behavior, and Release schema-3 complete-step/subscope timings with at least five independent processes per group. Retain raw results, configurations, provenance and limitations.

**Rationale**: The existing tests cover broad support, lifecycle, feature remapping and pair bookkeeping, but existing multicell witness lifecycle coverage is not established across worker counts and the 131-pair parallel queue case uses triggers rather than physical response. Existing benchmark scenarios need resolved-workload and candidate-pair-churn validation before being used to attribute a slot effect. Static code absence cannot quantify a speedup or prove graphical output.

**Alternatives considered**: Treat prior archived benchmark captures as the baseline for this new change (different source/fixtures); compare sanitizer/work-probe timing with release (different instrumentation); assert fixed percentage targets without an owner requirement (invented); silently count unavailable graphical/TSan or skipped checks as passing (unsupported).

## Unresolved clarifications

None. Public compatibility and the unobserved dropped-step accumulator decisions are recorded in the specification; a fresh actual consumer can instead justify retention with explicit evidence.
