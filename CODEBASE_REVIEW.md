**Platformator C++ review — 21 October 2026**

Reviewed commit `e01526b`, including the runtime/library extraction and the subsequent physics, deletion, and benchmark changes. The working tree was clean when the review began. This review adds documentation only; engine and test sources were left unchanged. Python received a smoke test only, following the requested emphasis on C++.

**Where the project stands**

Platformator is a substantial engine prototype with a working C++ build, a separately compiled game example, component and behavior APIs, rendering, audio, animation, serialization, collision detection, an impulse solver, sleeping, and benchmark infrastructure. The grid plus segmented sweep-and-prune implementation is a substantive part of the project. Contact feature tracking, warm starting, pair adjacency, and deferred broad-phase work show considerably more implementation depth than a minimal platformer framework.

The main gap is a consistent lifetime and mutation model. Operations that work during host setup become unsafe when called from scripts, collision handlers, animation callbacks, or shutdown. The public library interface exposes those operations without establishing when they are safe. This is the central issue behind the scene-management problem.

My assessment: a credible diploma implementation and demonstration base, with significant correctness work remaining before describing the C++ API as a stable reusable engine library. Preserve the existing systems and concentrate the next development phase on lifecycle correctness, reproducible tests, and clearly stated supported behavior.

**Verification performed**

| Check | Observed result |
| --- | --- |
| Fresh GNU C++20 Debug build, examples/tests/benchmarks enabled | Succeeded |
| Project C++ test executables, normal build | 3 passed; 2 failed |
| Failures inside the main regression executable | `sleeping_tall_stack_stays_asleep`, `sleeping_wide_pile_stays_asleep` |
| Deletion safety executable, normal build | Expected collision exit after destruction was missing |
| Project tests with AddressSanitizer and UndefinedBehaviorSanitizer | 3 passed; 2 failed; deletion test helper itself contains a use-after-free |
| Focused lifetime probes under sanitizers | Multiple engine use-after-free errors reproduced; details below |
| Install plus independent `find_package(Platformator)` consumer | Configured, compiled, and linked |
| Mario example consuming the installed library | Compiled and linked; launching from `/tmp` failed to find its adjacent scene |
| Public headers compiled individually | `baseobject.h` failed because it does not include `<string>` |
| Small broad-phase and add/remove benchmark runs | Completed; these were functionality checks, not performance measurements |
| Python smoke suite | 27 passed; 3 failed; no detailed Python audit |

The unfiltered CTest run also registered 140 vendored TBB tests whose executables were not built. Those are infrastructure failures, separate from the two failing Platformator executables. Use `ctest -R '^platformator_'` to isolate the five project executables until CMake is corrected.

Sanitizers instrumented the engine and project test sources; vendored dependencies were not instrumented. Leak detection could not run in this environment because LeakSanitizer reports that it cannot operate under ptrace. ThreadSanitizer and a full graphical gameplay session were not run. Parallelism findings below combine source inspection with a controlled one-worker/multiple-worker probe.

**Highest priority findings**

1. **P1 — Clearing a scene from a behavior can delete the behavior while its callback is running.**

   [Runtime::clearScene](/home/user/Projects/Platformator/src/runtime.cpp:61) destroys objects and calls `simulateFrame()` to drain deletion. That executes another simulation step inside the current callback. With multiple pending starts, calling it from `start()` produced a sanitizer failure at [triggerStartedBehaviors](/home/user/Projects/Platformator/src/gamemanager.cpp:735): the copied start list still contains freed behaviors. Calling it from `update()` also freed the currently executing behavior; accessing a member afterward produced a use-after-free.

   Scene operations need a request/commit model. A callback requests a transition, returns normally, and the runtime commits it at a defined boundary. Cleanup must have its own operation that does not run scripts, physics, or another frame. Establish whether remaining callbacks run after a transition request, and enforce that policy across every frame entry point.

2. **P1 — Script callbacks invalidate the component collections being iterated.**

   [The update loops](/home/user/Projects/Platformator/src/gamemanager.cpp:676) iterate `scriptComponents` directly. Adding another ScriptComponent can reallocate that vector. Removal uses swap-and-pop immediately. A probe adding 100 scripted objects from `update()` produced a sanitizer failure in `updateScriptComponents()`. In another probe, behavior A destroyed B while C remained alive; the observed update counts were **A=1, B=0, C=2** in one frame.

   Defer structural registration/unregistration during dispatch, or use a stable dispatch snapshot with validity checks and deferred physical destruction. Apply the same policy to fixed/update/late-update, start, collision, and animation callbacks. Track whether each behavior has started: creating a behavior on an inactive object and then activating it produced **0 starts and 1 update**.

3. **P1 — Component removal does not respect deferred physics lifetimes.**

   [GameObject::removeComponent](/home/user/Projects/Platformator/src/gameobject.cpp:253) unregisters and immediately deletes the component. Collider unregistration queues removal containing its raw pointer. The next grid flush dereferences that freed collider. Reproduced by adding a BoxCollider, simulating once, removing the collider, and simulating again; AddressSanitizer reported a use-after-free at [Grid::removeColliderInternal](/home/user/Projects/Platformator/src/grid.cpp:66).

   Component replacement has the same route through `removeComponent()`. Defer component destruction until physics bindings, pairs, queued events, and callback dispatch no longer reference it. Flushing arbitrary physics work inside a callback is not a general fix; it can invalidate the event currently being dispatched.

4. **P1 — Shutdown and destruction have inconsistent ownership and callback ordering.**

   [GameManager shutdown](/home/user/Projects/Platformator/src/gamemanager.cpp:35) queues every registered object in flat-vector order. [GameObject destruction](/home/user/Projects/Platformator/src/gameobject.cpp:21) then examines its child pointers. Creating a child before its parent, parenting it, and destroying the runtime produced a use-after-free: the parent inspected an already deleted child.

   Components are also deleted in enum order. Collider and Rigidbody precede ScriptComponent, whose destructor invokes `onDestroy()`. A probe reading its own BoxCollider in `onDestroy()` produced another sanitizer failure because that component had already been deleted.

   Give the runtime one clear ownership model for registered objects, detach hierarchy links before freeing storage, and dispatch `onDestroy()` once while the required object/component state is still valid. Define what mutations are allowed from `onDestroy()` and how newly queued work is drained.

5. **P1 — ObjectReference is a cached raw pointer that can dereference freed memory while resolving.**

   [getReferencedId](/home/user/Projects/Platformator/include/platformator/objectreference.h:40) reads the ID from the cached object. After that object is destroyed, even calling `resolve(runtime)` first reads freed storage rather than safely consulting the registry. This was reproduced under AddressSanitizer. `get()`, `operator->`, and the boolean conversion also retain stale state. Resolution uses an unchecked `static_cast<T*>`, so an ID referring to a different object/component type can produce an invalid typed pointer.

   Store a stable identity independent of the pointer. Resolve against the registry with type validation and an invalidation mechanism; a generation or scene epoch can prevent accidental rebinding after reload. Clearly distinguish raw observer pointers from references intended to survive object destruction.

6. **P1 — Parallel narrow phase writes shared Rigidbody state.**

   [Parallel pair processing](/home/user/Projects/Platformator/src/physicsmanager.cpp:179) calls collision support-state updates, which increment and decrement the same body's ordinary `supportContactCount` through [Collision](/home/user/Projects/Platformator/src/collision.cpp:159) and [Rigidbody](/home/user/Projects/Platformator/src/rigidbody.cpp:361). Multiple pairs involving one body are not independent, even though the result vectors are thread local.

   In a probe with one dynamic box and 128 static supporting boxes, the count after ten steps was **128 with one worker**, and **116 and 104 in two runs with 16 workers**. A separate uncapped run produced 170. These results agree with the unsynchronized shared writes in the source. The existing parallel regression uses isolated static pairs and does not exercise this shared dynamic-body state.

   Produce pair-local geometric/contact results in parallel, then reduce body-level state in a controlled phase. Keep a serial reference configuration for correctness comparisons. Also audit [parallel movement](/home/user/Projects/Platformator/src/physicsmanager.cpp:26): a child's `getPosition()` reads its parent's position while another task can write that parent. Compute transforms in hierarchy order or use immutable per-step snapshots. This second race was identified by inspection, not a ThreadSanitizer run.

**Other correctness work**

7. **P1 — Parent movement leaves descendant collider caches stale.**

   [setPosition/updateCollider](/home/user/Projects/Platformator/src/gameobject.cpp:161) marks only the current object's collider dirty, while child world positions are computed recursively. A probe moved a parent by 1000 units: the child reported world X=1010, but its collider's cached minimum X remained 5 instead of 1005 after a frame. A child with no Rigidbody is an ordinary case that exposes this immediately.

   Propagate transform dirtiness to descendants and update their broad-phase membership. The existing [hierarchy regression](/home/user/Projects/Platformator/tests/regression_tests.cpp:1751) calls `childCollider->prepareSync()` manually, which hides the missing runtime propagation. Define translation, rotation, scale, parenting, and active-state inheritance explicitly; the current position hierarchy does not imply a complete transform hierarchy.

8. **P1 — Scene identity, load semantics, and ID registration need a contract.**

   [loadScene](/home/user/Projects/Platformator/src/gamemanager.cpp:71) appends objects. Scene is a file loader, not a runtime-owned scene with membership and an unload lifecycle. Loading a one-object scene twice produced three registered objects including the generated camera. Name lookup returned the first object, ID lookup returned the second, and deleting the first erased the ID entry for the still-live second. [Registration](/home/user/Projects/Platformator/src/gamemanager.cpp:159) overwrites duplicate IDs; removal erases by ID unconditionally. Public `setId()` can also change a registered object's ID without repairing the map.

   Distinguish replacement from additive loading. Either reject duplicate serialized IDs or remap them consistently, including references. Keep runtime identity separate from file-local identity if additive loading is supported. A minimal diploma scope could support one active scene with safe deferred replacement; multiple independently unloadable scenes are an additional design commitment.

   [Scene loading](/home/user/Projects/Platformator/src/scene.cpp:92) also allocates objects/components with raw `new` while deserializing. Exceptions can abandon partially constructed objects and earlier roots; staged ownership should clean these up. Missing files return an empty collection and the runtime updates `currentSceneFilePath` before knowing whether loading succeeded. Load into owned staging data, validate, resolve, and commit only on success. Save errors should be observable, and saving through a temporary file would avoid truncating a working scene on failure.

9. **P2 — Activation and collision-filter changes can leave physics registration wrong.**

   Disabling and re-enabling a collider's object before the next flush queues REMOVE, while [queueAddCollider](/home/user/Projects/Platformator/src/grid.cpp:488) refuses ADD because an operation is already queued. The flush removes the collider although the object is active. The overlap probe emitted an exit and never re-entered.

   [Collision group/mask setters](/home/user/Projects/Platformator/src/collider.cpp:141) contain commented-out refresh logic. Filtering is applied when broad-phase pairs are introduced, so existing pairs can remain active after becoming disallowed. Setting an overlapping collider's mask to zero and moving its partner still produced a stay with no exit. Reconcile each collider's desired final state at a synchronization point, and invalidate pairs when filters change.

   Define collision event semantics too: unchanged overlapping objects in the static probe emitted one enter and no stays over subsequent frames. The implementation updates changed pairs, so event dispatch should not silently inherit its cadence from broad-phase dirtiness. Destruction-driven exit policy also needs to agree with the tests.

10. **P2 — Circle/box SAT can report collisions outside a corner.**

    [CircleCollider::getNormals](/home/user/Projects/Platformator/src/circlecollider.cpp:43) supplies the center-to-center axis; BoxCollider supplies its face normals. For a circle against a box, these are insufficient around corners. A 20x20 box centered at (0,0) and a radius-1 circle centered at (10.95,10.4) produced a collision enter, although the nearest corner is approximately 1.031 units away.

    Implement circle/box contact generation using the box-local closest point, or include the appropriate circle-to-closest-vertex separating axis. Add corner-grazing, containment, rotated-box, and offset cases. Also define valid ranges for dimensions, scale, mass, friction, and timestep; unchecked public values can lead to invalid projections or numerical results.

11. **P2 — The main loop's fixedUpdate is not a fixed simulation timestep.**

    [loop](/home/user/Projects/Platformator/src/gamemanager.cpp:322) passes elapsed wall-clock time through one physics step and one `fixedUpdate()` per rendered frame. A frame cap does not make this fixed, and there is no accumulator or long-frame policy. Benchmark stepping uses an explicit constant dt, so benchmark behavior and interactive behavior can differ.

    Introduce a fixed-step accumulator if that is the intended API, with a bounded catch-up policy. Otherwise name and document the variable-step behavior honestly. Use one underlying frame pipeline for the ordinary loop and custom stepping paths; their duplicated sequences already differ in deletion/render ordering.

**Tests need repair alongside the engine**

The test suite contains useful geometric, contact-cache, sleeping, and deletion scenarios. Its presence is valuable, but the current result is not a passing baseline.

- The deletion suite's [containsGameObjectPointer helper](/home/user/Projects/Platformator/tests/queued_deletion_safety_test.cpp:31) obtains an ID by dereferencing a pointer that its SceneScope retains after deletion. AddressSanitizer stops in the test helper itself. Store IDs when the objects are alive and validate against those, or use safe handles. This sanitizer failure must not be attributed to the engine's deletion code.
- [testSceneVersion2RejectsInvalidIds](/home/user/Projects/Platformator/tests/scene_script_roundtrip_test.cpp:417) accepts zero IDs and duplicate component IDs. Its name describes validation it does not perform. Uniqueness is needed by the current global registry even if zero is a valid ID by policy.
- The hierarchy test manually repairs the exact cache the runtime should repair, as described above.
- The expected exit in [testLateFrameExitEventDeletion](/home/user/Projects/Platformator/tests/queued_deletion_safety_test.cpp:298) is missing. Its failure message calls the situation a duplicate exit, but the assertion is checking for exactly one. Correct the diagnostic and decide the lifecycle contract.
- Commit `2276362` removed the support-removal wakeup regression. Restore coverage of that behavior against the current sleeping API; do not blindly restore obsolete constants.
- Most tests have access to private implementation headers. Retain focused internal algorithm tests, and add public-API integration cases for scripted scene replacement, component replacement, callback mutation, references after deletion, hierarchy shutdown, activation churn, and failed loads.
- Add a small CI configuration covering normal tests and ASan/UBSan. Add a targeted TSan job after deciding how the TBB runtime is instrumented. Split the 1908-line regression executable by subsystem when maintaining it becomes easier that way; a framework migration is not required first.

**Library, build, and asset packaging**

The library extraction has achieved a real result: the public include directory, static engine target, Runtime entry point, and independent consumer builds work. Installed consumption was verified with both a minimal host and the Mario source project. Runtime still relies on global active-runtime access and SDL process-wide initialization/teardown, so document the single-runtime and SDL ownership constraints for embedding.

The next build cleanup should be specific:

- Disable vendored TBB tests explicitly for ordinary engine builds. `TBB_TEST` defaults to ON here, registering 140 tests excluded from the ordinary build.
- Remove the generated `platformator/detail` forwarding-header machinery after confirming no external consumer depends on it. Nothing in the current sources, public headers, examples, or tests includes those generated wrappers. Most `src/*.h` public-header shims can also be removed as includes are normalized.
- Make `baseobject.h` self-contained with its standard includes, and retain a compile check for each public header.
- Prefer exported CMake targets and proper dependency discovery over the handwritten `find_library` dependency list. The current approach worked on this Linux host; cross-platform and static-dependency configurations were not validated.
- Give the runtime an explicit asset root. [PathManager](/home/user/Projects/Platformator/src/pathmanager.cpp:15) currently considers an assets tree valid only when it contains specific directories and `scenes/default.scene`. The May 30 cleanup removed the core audio/animation fallback files, while building Mario copies game assets into the shared output directory. A successful full build can therefore mask missing standalone runtime assets.
- The installed-library Mario consumer compiled, but running it from `/tmp` logged `Failed to load /tmp/assets/scenes/mario_example.scene`, despite a scene beside the executable in its own assets directory. Its copied tree fails the root-validity assumptions. Fix the example's installed-prefix calculation too: two parents above `lib/cmake/Platformator` reaches `lib`, not the package prefix.
- Preserve filesystem case in asset paths on case-sensitive systems. Current canonicalization lowercases paths before filesystem lookup. Decide case-insensitive logical asset naming separately from the actual disk path.
- [SDL initialization checks](/home/user/Projects/Platformator/src/sdlwindow.cpp:97) use SDL2-era numeric comparisons even though vendored SDL3 `SDL_Init()` and `TTF_Init()` return bool. Runtime construction does not surface window initialization failure. An invalid-video-driver startup plus a missing scene returned exit code 0 in a probe. Use the correct boolean checks and propagate startup/load failures to the host.
- Refresh README commands and repository guidance: README references the removed `assets/scenes/mario.scene`, while `.github/copilot-instructions.md` still describes SDL2, Make, and an empty collision resolver.

**Recent changes and what they imply**

| Change | Assessment |
| --- | --- |
| `9f3d207` and follow-up cleanup commits | Established the runtime and public library surface. Useful progress, but largely retained the existing global manager and lifetime assumptions. |
| `ee1bd5c` hierarchy movement change | Removed eager descendant position writes. Current code still needs descendant collider invalidation and safe parent/child parallel movement. |
| `7e71adc` deletion cleanup change | Centralized the physics flush before GameObject deletion. Component deletion still bypasses this protection. The collision-exit suppression also disagrees with the surviving deletion test. |
| `530798c`, `256dd4f`, `cb39450` | Added meaningful pair-cleanup work and regressions. These address individual broad-phase cases; they do not establish a general lifecycle contract. |
| `8af840d`, `814df8b`, `bf12108` | Expanded benchmark scenarios and exposed additional frame callback APIs. The runtime should have one well-defined stepping primitive before adding more variants. |
| `b910603` | Removed duplicated/demo assets and old result files, but also removed files assumed by fallback/root-discovery code. Asset packaging needs explicit ownership. |
| `e01526b` | Adds vendored Flecs only. No engine source or CMake integration uses it. This is not an ECS migration. Decide its purpose before carrying it as a project dependency. |

The commit history does not establish which authoring tool produced individual lines. The findings above concern observed behavior and code structure, not guesses about code provenance.

**Benchmarks and diploma claims**

Keep the benchmark infrastructure, but repair the measurement definition before relying on the graphs as end-to-end engine comparisons.

[The recorded frame interval](/home/user/Projects/Platformator/src/gamemanager.cpp:277) ends inside `runSimulationStep()`. [simulateFrame](/home/user/Projects/Platformator/src/gamemanager.cpp:352) then updates transient audio, flushes remaining physics operations, and deletes objects outside that interval. This omits real work from the add/remove benchmark, especially object/component destruction. Rendering is outside the interval too. Either measure the complete intended step or label the current interval precisely and report cleanup separately.

Godot's script records `Performance.TIME_PROCESS` and `TIME_PHYSICS_PROCESS`; Platformator records its own custom scopes. The source does not establish equivalent measurement boundaries. A defensible comparison should specify identical workloads, dt, warmup, sleeping policy, collision/event work, rendering policy, worker limits, engine versions, compiler flags, machine, and repeated-run uncertainty. Separate broad-phase algorithm measurements from total-engine claims. The current sweep script takes one run per population rather than repeated independent runs.

Correctness should also be checked during benchmark runs: expected population, finite transforms, valid pair counts, and consistent results under serial and parallel execution. A fast run that loses registrations or contact state is not useful performance evidence. No existing performance result was regenerated or certified by this review.

**Recommended repair order**

1. Establish a trustworthy baseline: fix test-helper use-after-free and CTest dependency pollution; preserve the currently failing engine tests as visible failures. Keep each fix narrowly scoped.
2. Define lifecycle rules and implement one deferred structural-mutation mechanism covering scene changes, object/component destruction, registration, starts, and `onDestroy()`. Make scene replacement from scripts an acceptance case.
3. Fix references and ID registration, including duplicate IDs, registered-ID changes, reference invalidation, and failed-load rollback. Make deserialization ownership exception-safe.
4. Fix hierarchy cache propagation, activation/filter reconciliation, and shared physics writes. Compare a deterministic serial path with the parallel path using shared-body scenarios.
5. Address remaining geometry, sleeping, and timestep behavior. Separate intended limitations from bugs and document supported semantics.
6. Consolidate runtime stepping, asset roots, install packaging, CMake exports, headers, and documentation. Keep benchmarks outside the normal runtime implementation where possible.
7. Re-run and document benchmark experiments only after correctness is established. Treat further GUI work and a possible ECS redesign as separate follow-up projects.

For diploma completion, I would target a fresh build, a passing project test suite, sanitizer-clean lifecycle scenarios, repeated scripted scene replacement, a standalone runnable game example, and reproducible benchmark methodology. Those provide a stronger finishing point than additional engine features.

**Reproduction artifacts from this review**

The build and probes were kept outside the repository. These paths are local review artifacts and may disappear when `/tmp` is cleaned:

- [Standalone probe source](/tmp/platformator-review-probe.cpp), including scene clearing, callback mutation, component removal, references, hierarchy, activation/filter, corner-contact, and shared-support cases.
- [Sanitizer CMake hook](/tmp/platformator-review-sanitize.cmake).
- [Normal build log](/tmp/platformator-review-build.log), [normal CTest log](/tmp/platformator-review-ctest.log), and [sanitized project CTest log](/tmp/platformator-review-ctest-asan.log).
- Per-probe sanitizer reports: `/tmp/platformator-review-probe-<case>.log`; worker comparison reports add `-1` or `-16` before `.log`.

Initial build commands:

```sh
cmake -S . -B /tmp/platformator-review-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPLATFORMATOR_BUILD_EXAMPLES=ON \
  -DPLATFORMATOR_BUILD_TESTS=ON \
  -DPLATFORMATOR_ENABLE_BENCHMARKS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /tmp/platformator-review-debug --parallel 4
ctest --test-dir /tmp/platformator-review-debug -R '^platformator_' --output-on-failure
```

Sanitizer build used the same directory after the initial normal run, with the supplied CMake hook and `-fsanitize=address,undefined -fno-omit-frame-pointer`. Leak detection was disabled for memory-access checks because of the environment limitation stated above.
