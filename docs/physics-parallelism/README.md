# Physics parallelism audit and implementation plan

The audit used Platformator commit `92c77618` in a Linux Release build. The timings cover headless simulation. The [original report](evidence/2026-11-20/report.json) records the build identity, workloads, commands, component timings, and graph observations. The raw samples are beside it.

## Findings

| Workload | Observation |
|---|---|
| Dense trigger fixture, six workers | Averaged 116,042 pending pairs and 163,622 active contacts. The serial narrow-phase pass fell from 4.45 ms to 1.17 ms, and the complete step from 9.17 ms to 5.82 ms, in a prototype that skipped redundant bookkeeping. These are medians of three run means. |
| One 1,000-body container | The largest solver island accounted for 89% of writable constraints across the capture and 97% in the last 300 steps. In the late steps, an order-preserving schedule had a median of 11 batches per iteration and 291 constraints in its largest batch. Greedy coloring gave six batches and 413 constraints in its largest batch. |
| Eight separate containers, 1,000 bodies total | The largest solver island accounted for 3.4% of writable constraints across the capture. |
| Transform synchronization | About 10 µs per step in the dense trigger fixture and 17 µs in a single six-worker container run. Both fixtures were headless. |

The narrow-phase prototype matched dense per-step counters. One-worker checks also matched snapshots and counters for the trigger and rigid-body fixtures, with per-step support invariants enabled. The island runner crashed because its worker lambda accessed worker-local scratch vectors that were empty. The graph traces came from the serial solver and measure available independent work. Parallel solver speed remains unmeasured.

The report preserves the six-worker container baseline summary, including the 17 µs transform figure. A later one-worker validation run reused its filenames. The archived `single_container-serial-0.samples.csv`, `single_container-serial-0.profile.csv`, and `single_container-serial-0.csv` belong to that validation run. The six-worker raw baseline samples were overwritten. The dense timing samples and both graph traces are intact.

## Implementation results

| Change | Six-worker Release step time |
|---|---|
| Narrow-phase fast path, dense triggers | 9.33 → 6.13 ms. All per-step counters matched. [Capture](evidence/implementation/narrow-phase/report.json) |
| Solver islands, eight containers | 0.337 → 0.281 ms. Median solver iteration time fell from 119 to 41 µs, with grouping averaging about 8 µs per step. [Capture](evidence/implementation/solver-islands/report.json) |
| Order-preserving batches, one container | 0.715 → 0.644 ms against island scheduling alone. Median solver iteration time fell from 330 to 260 µs. [Capture](evidence/implementation/solver-ordered-batches/report.json) |

Timing figures are medians of three run means. The solid-body narrow phase fell from 96 to 91 µs. Complete step timings were mixed and the solid-body contact trajectories varied across runs. The solver comparisons use the same instrumented binaries within each fixture. Same-state replay matched velocities and cached impulses bit for bit on 532 parallel steps across the validation runs, and the runtime's per-step invariant checks passed. The initial ordered-batch summary check counted both dispatch types together. The recorded replay conditions were verified per row before the timing runs. [Island replay](evidence/implementation/solver-islands/parity.json) · [Batch replay](evidence/implementation/solver-ordered-batches/parity.json)

Transform traversal remains serial. The sprite-heavy probe stopped during setup because the texture path was missing the `assets/` prefix. The harness source was corrected and the failed capture is retained. No timing sample was produced. [Transform probe](evidence/implementation/transform-sync/report.json)

### Review fixes

Both parallel solver paths use isolated oneTBB cancellation contexts. Island jobs group small islands by constraint count and put expensive islands into separate jobs. Scheduler setup resets only participating body slots when the registered population is more than four times the constraint count. The regression suite covers a cancelled caller, dense and sparse ordered-batch parity, and the unbalanced island layout across registered-body slot changes.

In the focused six-worker solver probe with 256 contacts and 50,000 unrelated static bodies, the median run mean fell from 98.2 to 80.2 µs, with all three paired runs improving. With no unrelated bodies the median went from 89.0 to 82.9 µs and paired results were mixed. Sampled velocities matched across all runs. [Review verification](evidence/review/verified-fixes.json)

## Implementation plan

1. **Implement the narrow-phase fast path.** In [`PhysicsManager::narrowPhase`](../../engine/src/physicsmanager.cpp#L279), read the post-test contact once. Skip per-pair support and participation processing when a pair stays out of contact and has no membership or support credit, or when a live trigger contact persists with valid contact membership and no solver membership or support credit. Keep the current commit path for transitions and any uncertain case. Preserve the pending-pair order, the work probes, and the final net support flush. This removes repeated component lookups and membership checks, with a measured 37% reduction in complete-step time in the dense trigger fixture. Check subscribed enter and exit order, triggers becoming solid, body replacement, retirement, and net support transfers. Compare paired Release step and narrow-phase timings in the dense trigger fixture and a solid-body fixture, including p95. If the remaining serial scan is a material cost, try worker-written change flags indexed by pending-pair position. Commit flagged pairs in their original order and measure again before considering a storage migration.

2. **Test island scheduling for disconnected solver work.** In [`PhysicsManager::resolveCollisions`](../../engine/src/physicsmanager.cpp#L367), initially keep preparation, warm starting, kinematic correction, and sleep bookkeeping serial. Group prepared constraints by the writable dynamic bodies they share. A shared static or kinematic body can be read by several groups after preparation. Build the groups with reusable per-step union-find scratch indexed by registered body slot, append constraints in original order, and dispatch coarse groups that each run all ten iterations. Give workers explicit access to owner-prepared arrays and join before reconciling bodies. Use a serial path when a scene has little work, and use the measured crossover to decide when building or dispatching groups pays off. The expected gain is lower iteration time in scenes with many independent contacts. Compare serial and parallel iteration from the same prepared state and require exact velocities and cached impulses. Time preparation, grouping, iterations, and complete steps in the one-container and eight-container fixtures. Use the isolated control to detect scheduling overhead and the large pile to detect an unhelpful dispatch.

3. **Test order-preserving batches for a dominant island.** Walk its prepared constraints in existing order and assign each to one batch after the latest batch on any writable endpoint. Execute the independent ranges within each batch and synchronize before advancing. Reuse the schedule for all ten iterations. The one-container graph had enough independent work to justify a pilot, while the repeated batch barriers may absorb the gain. Use same-state parity and include batch construction and barriers in the timing. If barriers erase the gain, compare a deterministic greedy coloring with sized ranges and a serial overflow. Coloring can change contact solve order and numerical results, so require comparable penetration, settling, and sleep behavior on stack, friction, restitution, and kinematic cases before enabling it. Jolt uses conflict-free groups to split large islands. Box2D persists colors and uses them for parallel and SIMD solves. [Jolt architecture](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md#solve-velocity-constraints) · [Box2D graph coloring](https://box2d.org/posts/2024/08/simd-matters/)

4. **Gate transform parallelism on a representative scene.** Measure the position walk and publication in a sprite-heavy moving hierarchy. If the walk is material, batch the disjoint topmost dirty subtrees, compute positions parent-first on workers, and replay changed-object notifications on the simulation thread in the original order. Verify immediate reads, reparenting, parent and child writes in the same step, collider synchronization, sleep and wake behavior, and rendered positions. The headless captures suggest a small gain at current costs, and the extra staging pass can offset it. [Transform synchronization](../../engine/src/gamemanager.cpp#L721)

## Reproducing the completed checks

The [audit tools](tools/) rely on Linux CPU affinity, a Ninja Release benchmark build at `build/benchmark`, and its compilation database. Use the audited engine revision for direct comparisons. Run the command from the repository root after configuring and building that target.

```sh
python -B docs/physics-parallelism/tools/run.py
```

The command writes a fresh capture to `docs/physics-parallelism/evidence/reproduction/` by default. Pass `--output` to choose a different new directory. It reproduces the completed narrow-phase comparison, serial timing baseline, graph observations, and invariant checks. Each capture is limited to 50 MiB and 600 seconds. Generated source files are temporary, and the compiled runner goes under `build/parallel-physics-audit/`. The archived report retains the original paths and script hashes as provenance. Resolve sample filenames relative to the archived report.

For the implementation comparisons, `compare_implementation.py --output PATH` uses the saved narrow-phase binaries in `build/parallel-physics-implementation/`. Run `solver_implementation.py islands --output PATH` or `solver_implementation.py ordered-batches --output PATH` to build an instrumented runner and record the corresponding comparison. Each command creates a fresh output directory before starting work.
