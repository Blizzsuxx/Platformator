# Code cleanup audit

Recorded: 26 October 2026. Reviewed baseline: `61949f4ef8766bef04a5c4099d858ef56ef9d21f`.

This document records the cleanup investigation and subsequent history tracing. It is a backlog for creating several focused feature specifications, not an implementation plan or authorization to change runtime behavior.

## Scope and evidence

The investigation covered the C++ engine and public headers, usage in tests and benchmarks, the Mario example, and selected Python editor paths. Third-party implementations and historical evidence snapshots were excluded from the current-code usage search. The reference commits were:

- `61949f4` — game manager cleanup.
- `daa911d` — physics cleanup.
- `f6d1af7` — removed stay events.

Follow-up tracing searched all 275 commits reachable in the repository at review time for consumers of the pair slot table, accumulated bias impulse, and Audio manager index. Box2D-Lite's upstream solver was also inspected.

No implementation changes, new builds, runtime tests, or performance measurements were made during this audit. Findings describe source behavior at the baseline above. Existing reports and benchmark results are historical evidence, not new validation of these proposed changes. The older root-level `CODEBASE_REVIEW.md` describes an earlier baseline and must not be treated as a list of current unresolved bugs.

Classification:

- **Confirmed unused**: no current consumer, or an internal call chain with no reachable caller.
- **Simplification**: behavior-preserving reduction appears possible, subject to preserving the stated contracts.
- **Design decision**: a public API or user-visible behavior needs an explicit policy before removal/change.
- **Performance candidate**: avoidable work is visible in source; the size of any improvement is unmeasured.

Absence of repository callers does not prove absence of external SDK users. Public-header removals need a compatibility decision. Stable finding IDs below can be referenced by future specifications.

## Confirmed unused state and internal code

| ID | Finding and evidence | Suggested disposition / boundary |
| --- | --- | --- |
| C01 | **Collision-pair slot table.** `Grid::pairSlots`, `freePairSlots`, and `ColliderPair::pairSlot` are maintained during [insertion/removal](../src/grid.cpp#L172), but no consumer retrieves the stored pair pointers. | Remove the whole mechanism together. Preserve contact, solver, adjacency, and narrow-phase queue indexes. See the detailed trace below. |
| C02 | **GameObject hierarchy depth.** [Declaration](../include/platformator/gameobject.h#L136); [assignment during hierarchy rebuilding](../src/gamemanager.cpp#L916). The value is never read. | Remove the field, initialization/assignments, and recursive depth parameter. Preserve `hierarchyOrder`, which sorts dirty transforms. |
| C03 | **Accumulated normal impulse for bias.** [Field](../include/platformator/collisiontypes.h#L120); [warm-start copying](../src/collision.cpp#L59). It is initialized/copied and checked by tests, but never used in solver calculations. | Remove the field and its copying/preservation assertions together if public layout/API changes are accepted. Preserve `bias`, normal/tangent impulse accumulation, and both normal solver paths. See the detailed trace below. |
| C04 | **Audio manager index.** [Field/API](../include/platformator/audio.h#L49); [accessors](../src/audio.cpp#L215). The engine never assigns or consumes the index. | Remove the field/accessors if the public API change is accepted. No audio component collection needs this index. See the detailed trace below. |
| C05 | **Unobserved dropped-step accumulator.** [Declaration](../src/gamemanager.h#L186); [only update](../src/gamemanager.cpp#L325). `droppedSimulationSteps` is never reported or otherwise read. | Decide between exposing useful telemetry and deleting the manager accumulator. Preserve SimulationClock's actual catch-up/drop policy and clock tests. |
| C06 | **Unused private GameObject constructor.** The [parameterized constructor](../src/gameobject.cpp#L21) has no callers. Live and staged creation use the default constructor and setters. | Remove the private overload and declaration. |
| C07 | **Unused private GameManager add helper.** [GameManager::addGameObject](../src/gamemanager.cpp#L71) has no callers. Creation uses `registerGameObjectSubtree` directly; scene publication has its own path. | Remove this wrapper, preserving the registration implementation. |
| C08 | **Unreachable component-removal notification chain.** [GameObject::removeComponentsFromGameManager](../src/gameobject.cpp#L406) has no callers. Its call is the only consumer of [GameManager::notifyComponentRemoved](../src/gamemanager.cpp#L479). | Remove the two helpers together. Preserve active removal through retirement and reconciliation. |
| C09 | **Unused private collision support accessors.** [Four getters/setters](../src/collision.cpp#L188) have no callers; PhysicsManager uses support state directly. | Remove the methods/declarations. Support flags, support-state computation, and contribution reconciliation remain necessary. |
| C10 | **Unused grid synchronization helper.** [Grid::finishColliderSync](../src/grid.cpp#L408) has no callers. Active flushing already queues affected pairs. | Remove the helper/declaration without changing flush ordering. |
| C11 | **Unused ColliderPair mutation methods.** [Endpoint setters](../src/colliderpair.cpp#L90) and [single-step witness increment/decrement methods](../src/colliderpair.cpp#L132) have no callers. | Remove these internal methods. Endpoints are established by construction; current witness updates use `addToWitnessCount`. |
| C12 | **Unused projection rebinding chain.** [AABB::ColliderBinding::bind](../src/aabb.h#L74) has no callers. It is the sole caller of `BoundingRadiusProjectionAxisBinding::bind`, which is the consumer of `setBoundingProjection`. | Remove the chain if public projection API changes are accepted. Keep binding construction and projection cache updates. |
| C13 | **Unused resolved requested-path string.** [ResolvedAssetPath::requestedPath](../src/pathmanager.h#L19) is populated but never read. | Remove the field and corresponding aggregate values, avoiding a string copy per resolution. Keep the original request available locally for diagnostics. |
| C14 | **Unused direct debug rendering implementation.** [DebugDraw::drawBoxCollider through drawCross](../src/debugdraw.cpp#L71) is an uncalled group. Circle/cross helpers are reachable only from the other dead methods. | Remove these private declarations/definitions. The active `add*DebugObject` plus buffered rendering implementation remains. |

## Detailed trace: C01 collision-pair slots

### Intended purpose

Commit `7906959` introduced `pairSlots`, `freePairSlots`, and `pairSlot` together. The original [research decision](../specs/001-cpp-engine-cleanup/research.md#L77) requested reusable result/scratch buffers indexed by stable pair slots. [Task T057](../specs/001-cpp-engine-cleanup/tasks.md#L150) explicitly requested stable pair/result slots.

Such an arrangement can assign a pair a stable integer index into a result array, allow workers to write separate result entries without shared appends, and reuse holes after removal. That is a legitimate design when the result arrays or other consumers actually use the index.

### Actual execution

1. `candidateCollisions` owns pairs and finds them by collider key.
2. Adjacency lists and the pending narrow-phase vector hold direct pair pointers.
3. [Workers](../src/physicsmanager.cpp#L192) receive those pointers and update pair-owned manifold/result state directly.
4. [Contact/solver membership](../src/physicscontacts.cpp#L99) uses separate `contactIndex` and `solverIndex` fields.
5. [Removal](../src/grid.cpp#L238) clears queue/adjacency/contact/solver memberships and erases the pair from `candidateCollisions`.
6. The slot mechanism only allocates/recycles numbers, writes pointers, and clears pointers. No code retrieves a stored pair pointer or indexes result storage through `pairSlot`.

The table had no such consumer in its introducing commit either. The later [physics data model](../specs/003-simplify-physics-bookkeeping/data-model.md#L37) retained it for oracle/candidate enumeration consumers, but those consumers are absent from the current implementation. Documentation is not evidence of a live code dependency.

**Assessment:** unused infrastructure, with no identified missing runtime operation to restore. The likely explanation is that the planned slot-based result design and the implemented pointer-based pair-local result design overlapped, and the unused half survived. This is an inference from the artifacts, not knowledge of the author's or generating model's reasoning.

**Performance:** extra insertion/removal work, potential vector growth allocations, a per-pair member, and pointer/free-index vector capacity. Unchanged contacts incur retained memory but no direct per-frame slot-table traversal. It currently accelerates neither solving nor deletion. Removing it is expected to reduce memory and modestly reduce churn costs; magnitude requires measurement. Do not remove the other, actively consumed pair indexes.

## Detailed trace: C03 accumulated bias impulse

Commit `3a0d4ff` ("implementing sequential impulses") introduced the field and its warm-start copying. No historical solver computation using it was found.

There is a strong match to Box2D-Lite:

| Platformator | Box2D-Lite |
| --- | --- |
| `accumulatedNormalImpulse` | `Pn` |
| `accumulatedTangentImpulse` | `Pt` |
| `accumulatedNormalImpulseBias` | `Pnb` |
| `bias` | `bias` |

Upstream [Arbiter.h](https://github.com/erincatto/box2d-lite/blob/master/include/box2d-lite/Arbiter.h) declares `Pnb`; [Arbiter.cpp](https://github.com/erincatto/box2d-lite/blob/master/src/Arbiter.cpp) initializes/copies it but does not apply it in the solver. The resemblance supports the user's recollection of a Box2D-derived implementation; it does not prove exact provenance. These upstream links refer to the versions inspected during the audit and can change.

Platformator's real penetration correction is already connected:

1. [Pre-step](../src/physicsmanager.cpp#L403) computes Baumgarte penetration bias and restitution bias, then stores their sum in `bias`.
2. [The scalar normal solver](../src/physicsmanager.cpp#L505) includes `bias` in the impulse calculation, clamps the accumulated normal impulse, and applies its change.
3. [The two-contact block solver](../src/physicsmanager.cpp#L455) also includes each contact's `bias` in its equations.
4. Normal and tangent accumulated impulses are used for warm starting. The separate accumulated bias field is not.

This is consistent with the Baumgarte sequential-impulse approach described in Erin Catto's [Solver2D discussion](https://box2d.org/posts/2024/02/solver2d/).

**Assessment:** copied unused state, with no evidence that the current solver is missing a required bias step. Removing it does not remove penetration correction. A separate bias/split-impulse solver would be a new behavior change and should not be bundled into cleanup. This trace does not certify every other aspect of the solver's numerical correctness.

The assertions in `tests/regression_tests.cpp` that preserve artificial nonzero bias-accumulator values test storage behavior, not a physical solver effect. Adjust those assertions if the field is removed; preserve normal/tangent feature remapping and warm-start coverage.

## Detailed trace: C04 Audio manager index

Commit `a44f1b4` added Audio's index alongside the animator/script index optimization. That commit wired indexed swap-removal for animators and scripts but did not create an audio component vector or assign Audio an index. No later consumer was found.

Current lifecycle:

1. [Runtime addition](../src/gamemanager.cpp#L484) optionally starts Audio playback.
2. Runtime removal/deactivation stops playback.
3. [Component removal](../src/gameobject.cpp#L292) clears the fixed GameObject component slot and queues retirement when live.
4. [Retirement](../src/gamemanager.cpp#L1096) deletes the component directly. Its destructor releases the audio reference and its owned mixer track is destroyed.

There is no Audio vector to search or swap-remove, so the index stays `SIZE_MAX` under normal engine use. Removing it does not introduce a linear search or disable an existing fast-deletion path.

Fire-and-forget playback is separate: SDLWindow owns transient playback records, scans for completion, and [swap-removes the completed record](../src/sdlwindow.cpp#L494) using the current scan index. It does not require an Audio component index.

**Assessment:** an unused addition accompanying valid optimizations for other component types. Remove it if the public API change is accepted. Preserve Audio stop/release behavior and transient playback lifetime guarantees.

## Simplification and duplication candidates

| ID | Finding and evidence | Required constraint / decision |
| --- | --- | --- |
| S01 | **Axis plumbing without behavior.** [SegmentedIntervalList::axis](../src/segmentedintervallist.h#L59) is forwarded to [AABB overlap handlers](../src/aabb.cpp#L102), which ignore it. | Remove the stored field and unused constructor/handler arguments together. Preserve `isPrimary`, which changes checkpoint/collision processing. |
| S02 | **Unreachable required-fallback checks.** Texture, audio, and animation loaders test `required && resolved.usedFallback`, e.g. [texture loading](../src/gamemanager.cpp#L383). [Path resolution](../src/pathmanager.cpp#L98) throws for missing required assets before fallback selection. | Remove duplicate checks while preserving required/optional asset error and fallback semantics. |
| S03 | **Ignored penetration constructor argument.** [Collision constructor](../src/collision.cpp#L21) accepts but never uses `penetration`. Actual penetration is represented through contact separation. | Decide public API compatibility, then remove or deliberately redefine the parameter. Do not invent a second penetration authority. |
| S04 | **Duplicate animation clip getter names.** [getCurrentAnimationClip](../src/animator.cpp#L46) and [getCurrentClip](../src/animator.cpp#L183) return the same pointer. | Choose one public name; migrate callers or retain a deliberate compatibility alias. |
| S05 | **Duplicate behavior lookup helper.** [Free helper](../src/helpers.h#L73) repeats [Behavior::getBehavior](../include/platformator/behaviorquery.h#L7). No caller of the free helper was identified. | Remove the unused internal duplicate. Keep the public member template and its required includes. |
| S06 | **Repeated indexed vector swap-removal.** Manager animator/script lists, sprites, physics components, transforms, and adjacency repeat similar bookkeeping. [Body membership helper](../src/physicsbodyindexes.cpp#L5) shows an existing reusable pattern. | Consider a small internal utility only where semantics match. Startup cancellation deliberately leaves null queue entries during callbacks; do not replace it with generic swap-removal. Preserve moved-element indexes and iteration/callback safety. |

## Performance candidates requiring measurement or a behavior decision

| ID | Opportunity and evidence | Limits and validation focus |
| --- | --- | --- |
| P01 | **DebugObject copies instead of moving.** The [empty user-declared destructor](../src/debugdraw.cpp#L398) suppresses implicit move construction. Temporary insertion and buffer growth copy owned vertex/name data. | Remove the unnecessary destructor or supply appropriate move support. Measure with overlays enabled and growing buffers; preserve rendered output. This has no production benefit when debug tools are compiled out. |
| P02 | **Repeated parent-chain world-position queries.** [getPosition](../src/gameobject.cpp#L52) recursively computes world position despite a synchronization cache. Repeated reads/serialization in a deep chain can perform quadratic total traversal. | Cached reads are only safe at guaranteed synchronization points. Immediate reads after setters and reparenting must keep working; do not blindly return `worldPosition`. Exercise deep and shallow hierarchies and ancestor/child edits. |
| P03 | **Every active sprite is submitted for rendering.** [Sprite loop](../src/sdlwindow.cpp#L271), [camera rendering](../src/camera.cpp#L30). Camera scale is recomputed for each sprite, and off-screen sprites are submitted. | Prepare camera data once per render and investigate conservative culling. Preserve rotated/scaled/source-rect sprites, viewport edge cases, and draw order. Compare scenes with many off-screen sprites. |
| P04 | **Asset cache hits still resolve/check filesystem paths.** [Texture loading](../src/gamemanager.cpp#L383), with equivalent audio/animation ordering. | Consider resolution caching for repeated shared asset loads. Define invalidation and required-file behavior; preserve canonical spelling, configured root, symlink containment, fallbacks, and exceptions. Avoid introducing an unbounded cache without a lifetime policy. |
| P05 | **Repeated same-value setters do unnecessary work.** [Box dimensions](../src/boxcollider.cpp#L76), [circle radius](../src/circlecollider.cpp#L27), and Rigidbody mass/friction/restitution/gravity/body-type setters recalculate, wake, or enqueue without equality guards. | Establish whether repeated assignment should wake/reset state before adding guards. Validate invalid inputs before taking shortcuts. Preserve changes that really require wake, inertia refresh, geometry sync, or response updates. |
| P06 | **SDL_ttf dependency without font rendering.** [Initialization](../src/sdlwindow.cpp#L118), [link dependency](../CMakeLists.txt#L151). Engine usage is init/quit and benchmark version reporting, with no font rendering. | Decide whether to keep planned text support. Removal would span CMake, installed package dependencies, runtime-library copying, includes, initialization, and benchmark metadata. Validate package/consumer builds and metadata compatibility. |
| P07 | **Whole-scene undo copying and comparison.** [Snapshot recording](../ui/src/platformator_ui/main_window.py#L627) makes several deep copies and serializes before/after documents for each edit. | First reduce duplicate copies while preserving snapshot ownership and command merging. Property-level undo is a separate, larger design. Measure editing/dragging on large scenes and preserve selection, dirty/clean state, undo/redo, and merged edits. |

## Mario example and editor findings

| ID | Finding and evidence | Suggested disposition / boundary |
| --- | --- | --- |
| E01 | **Unused Mario state/configuration.** [MarioCameraRig](../examples/mario/mario_camera_rig.h#L26) stores write-only `targetBody`; serialized `cameraLead` never affects movement. [MarioPatrolEnemy](../examples/mario/mario_patrol_enemy.cpp#L37) assigns an unread `collider`. [Constants](../examples/mario/mario_constants.h#L10) include unused `STOMP_TOLERANCE`, `CAMERA_LEAD`, and `LEVEL_WIDTH`. | Remove dead pointers/constants. Decide whether camera lead is intended functionality or obsolete scene configuration; check scene files and behavior discovery before removing a serialized field. |
| E02 | **Undefined, unused MarioPlayer methods.** [getPositionBeforePhysics/getVelocityBeforePhysics](../examples/mario/mario_player.h#L30) are declared but neither implemented nor called. | Remove the declarations unless a separately specified feature needs them. |
| E03 | **Editor scene-directory policy leftovers.** [Inspector](../ui/src/platformator_ui/widgets/inspector.py#L163) stores `_scene_path_provider` without invoking it. [Canvas](../ui/src/platformator_ui/widgets/scene_canvas.py#L101) stores `_scene_path` without reading it. | Remove unused parameters/state and update callers/tests. Preserve actual `MainWindow.current_scene_path`, which still supports open/save/run behavior. |
| E04 | **Unreachable behavior-template fallback.** [discovered_behaviors](../ui/src/platformator_ui/services/behavior_catalog.py#L137) starts with every scene-sample key. Therefore the set difference at line 180 is always empty. | Determine intended behavior for scene-sample-only scripts. Restoring fallback execution is a correctness change, not just dead-loop removal; specify it explicitly. |
| E05 | **Unused editor scanner.** [_scan_serializable_script_fields](../ui/src/platformator_ui/services/behavior_catalog.py#L189) has no callers. | Remove the private scanner if it is not needed by the selected discovery behavior. Preserve the scanners actually used for references/templates. |

## Suggested specification boundaries

These are possible bounded work packages. They can be split further; they are not a required sequence or newly created specifications.

| Candidate specification | Findings | Acceptance focus |
| --- | --- | --- |
| Physics bookkeeping cleanup | C01, C03, C09–C12, S01, S03 | Preserve geometry, Enter/Exit, support transitions, sleep/wake, warm starting, worker equivalence, and active indexes. Make the public API/layout decision explicit. Update documentation that claims the pair slot table has consumers. Measure pair churn separately from solver changes. |
| GameObject/runtime dead-code cleanup | C02, C05–C08 | Preserve creation, scene publication, transforms/reparenting, retirement, and catch-up policy. Decide telemetry separately from dead helper removal. |
| Audio/animation API cleanup | C04, S04 | Preserve audio ownership/stop/release, autoplay, fire-and-forget, animation playback, and scene round trips. Define SDK compatibility. |
| Debug rendering cleanup | C14, P01 | Preserve overlay output and debug controls; verify both debug-tools-on/off builds. Demonstrate reduced copying/allocation where practical. |
| Shared helpers and asset loading | C13, S02, S05, optionally S06/P04 | Start with the simple removals. Keep caching and generalized collection helpers separate if they enlarge scope. Preserve mutation and asset path contracts. |
| Transform/rendering efficiency | P02, P03, optionally P05 | Separate immediate-read semantics from synchronized fast paths. Measure deep hierarchies, active body/contact work, and mostly off-screen scenes. Avoid combining unrelated solver behavior changes. |
| Editor/example cleanup | E01–E05 | Specify sample-only behavior-template policy and serialized Mario field policy. Preserve editor path rules, scene round trips, discovery, and the example build/gameplay. |
| Editor undo efficiency | P07 | Preserve snapshot isolation, merged commands, selection and dirty state; measure large-scene interactions. |
| Optional text dependency removal | P06 | Explicit dependency/product decision; validate source and installed consumers, runtime packaging, and benchmark metadata. |

## Guardrails for later specification and implementation

- Recheck finding references against the then-current branch; line numbers and unused status can change after earlier cleanup packages land.
- Treat confirmed field/helper removal separately from a new implementation of the feature suggested by its name. In particular, do not add a slot-based result system or split-impulse solver to justify existing unused fields.
- Preserve useful indexes and distinct lifecycle states merely adjacent to a dead field. Similar names do not imply redundant purpose.
- Preserve callback-time eligibility checks and queue behavior. Apparent repeated checks can protect against mutations made by earlier callbacks.
- Use existing meaningful tests for behavior-preserving removals; adjust tests that only preserve obsolete state. Add behavior coverage where a policy is restored or changed, such as E04.
- Capture new performance evidence for changes presented as optimizations. Compare equivalent builds, workloads, worker counts, and instrumentation; separate correctness/work probes from timing runs.
- Record measured outcomes honestly. Visible source overhead does not establish a measurable end-to-end speedup.
