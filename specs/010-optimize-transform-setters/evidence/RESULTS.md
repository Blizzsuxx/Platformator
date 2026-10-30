# Transform Setter Pilot Results

## State and source identity

- Branch: `branch-without-bvh`; HEAD: `a2f587d1c1281277e1b5a1d33d6a47b62fdb5a0c`.
- The requested baseline revision matched HEAD. The initial tracked candidate diff SHA-256 was `fed9f598b88f2ce28db227297589d2cfda4ee524f300c85ec71d0d846ad654a9`; it contained only the provisional transform cache and setter changes. Initial untracked files were the feature's own specification, plan and contract files. No unrelated working-tree changes were present; nothing was reset or overwritten.
- Final tracked diff SHA-256: `ff0a20049df0df640a49d3a241564de3545bd416ed3fb63d6b2bec6dc8b98f9b`. Final Release source-manifest aggregates: baseline `c3f84644e8b9cb81a2a5fb1319fd0f76c54485965b1368320d60b7cc79334376`; candidate `b5a379e06cfcb89b01292de3f8ab87e77e88126601cc128eb083666ea24a9a1d`.
- Final timing probe binary SHA-256: baseline `60ff11af23014c2d1b18760d302daf65324ecae69ed306a38ba4d992be991cd6`; candidate `86e78c3c293d899a61ca4281a775883a108f28fd5fb53f07e7f6e6db049e50fa`. Work probe binary SHA-256: baseline `269df5317c9ebd50b809881467d2d82a0326100286c2fa7dcfb5494d43801f44`; candidate `644eef7722c0ae2c68188f9799b42b79c75faf0e867fa52505dc194ab6df1c4f`.
- Temporary sparse source inputs are under `/tmp/transform-setter-inputs.B3Md9S/{baseline,candidate}`. Unchanged paths are linked to the repository; feature changes are materialized in the overlays. Builds are under `bin/transform-setter-*`, outside this evidence directory.

## Host and builds

- Host: Linux x86_64, kernel `6.18.55-1-MANJARO`; AMD Ryzen 5 7535HS, 6 physical cores / 12 logical CPUs.
- Tools: GCC `16.2.1 20260810`, CMake `4.4.4`, Ninja `1.13.2`.
- Matched Release settings: `-O3 -DNDEBUG`, sanitizer `none`, debug tools off. Timing builds have work probes off; work builds have work probes on. Both enable the benchmark probe and disable runner, examples, tests and install. Initial builds used four parallel jobs; final incremental probe rebuilds used three.
- Direct workloads use one worker. Complete-step and physics scopes request one and six workers.

Configure commands were run for each baseline/candidate source overlay and build directory:

```sh
cmake -G Ninja -S /tmp/transform-setter-inputs.B3Md9S/baseline -B bin/transform-setter-baseline -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF -DPLATFORMATOR_ENABLE_WORK_PROBES=OFF -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
cmake -G Ninja -S /tmp/transform-setter-inputs.B3Md9S/candidate -B bin/transform-setter-candidate -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF -DPLATFORMATOR_ENABLE_WORK_PROBES=OFF -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
cmake -G Ninja -S /tmp/transform-setter-inputs.B3Md9S/baseline -B bin/transform-setter-work-baseline -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF -DPLATFORMATOR_ENABLE_WORK_PROBES=ON -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
cmake -G Ninja -S /tmp/transform-setter-inputs.B3Md9S/candidate -B bin/transform-setter-work-candidate -DCMAKE_BUILD_TYPE=Release -DPLATFORMATOR_ENABLE_BENCHMARKS=ON -DPLATFORMATOR_ENABLE_DEBUG_TOOLS=OFF -DPLATFORMATOR_ENABLE_WORK_PROBES=ON -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_TESTS=OFF -DPLATFORMATOR_INSTALL=OFF -DPLATFORMATOR_SANITIZER=none -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
cmake --build bin/transform-setter-baseline --parallel 4 --target platformator_transform_setter_probe
cmake --build bin/transform-setter-candidate --parallel 4 --target platformator_transform_setter_probe
cmake --build bin/transform-setter-work-baseline --parallel 4 --target platformator_transform_setter_probe
cmake --build bin/transform-setter-work-candidate --parallel 4 --target platformator_transform_setter_probe
```

The final incremental Release rebuilds completed successfully. A preflight check confirmed equal compiler, build type, optimization flags, sanitizer and debug-tool settings for each timing and work pair. A parser check now confirms typed CMake cache keys such as `CMAKE_BUILD_TYPE:STRING` are normalized correctly.

## Correctness and fixture validation

The final focused debug command was:

```sh
ctest --test-dir bin/cleanup-debug --output-on-failure -R 'platformator_(transform_hierarchy|destruction_contract|scene_io_contract|physics_geometry|physics_bookkeeping|physics_support|runtime_step)_test$'
```

Result: **7/7 passed** (1.51 seconds), covering hierarchy and workers, destruction, serialization, geometry, bookkeeping, physics support and runtime stepping. An earlier run exposed one extra iteration in the new hierarchy test's final-frame assertion; the loop was corrected and the complete focused suite passed. The repository's separate all-target cleanup build has a known render-culling index test dependency on work probes; this feature uses the selected targets and does not claim that index test passed.

Release fixture smoke checks produced identical hierarchy checksum `4516000` and matching parent/child movement results. At six workers both revisions reported expected child x `40.799999` and actual x `40.800041` after the measured frame fixture. New motion-setter, setter-heavy step and corrected physics-scope workloads matched identity and correctness signatures across baseline and candidate. Both revisions retained expected final setter values and wake-threshold outcomes. These smoke checks validated outputs and timing boundaries before the capture.

## Separate work and resource observations

The instrumented `--case work --workers 1` case is explicitly untimed. It ran once per build variant.

| Observation | Baseline | Candidate |
| --- | ---: | ---: |
| `sizeof(GameObject)` | 272 bytes | 280 bytes |
| Deep immediate reads (1,000 reads, depth 64): position visits | 64,000 | 1,000 |
| Deep ancestor edits (10 edits, depth 64): immediate refresh visits | 0 | 640 |
| Same-value setter pass (100 iterations): collider sync requests | 500 | 0 |
| Same-value setter pass: body participation enqueues | 1 | 0 |
| Same-value setter pass: collider queued / body sleeping afterward | true / false | false / true |
| Same-value velocity/angular setter pass (100 iterations): body participation enqueues | 1 | 0 |
| Changed velocity/angular control (100 iterations): body participation enqueues | 1 | 1 |

Allocation and object-storage observations:

| Objects | Variant | Object storage | Fixture allocations | Allocated bytes | Peak RSS |
| ---: | --- | ---: | ---: | ---: | ---: |
| 64 | Baseline | 17,408 B | 147 | 24,840 B | 27,236 KiB |
| 64 | Candidate | 17,920 B | 147 | 25,352 B | 27,236 KiB |
| 1,024 | Baseline | 278,528 B | 2,078 | 385,696 B | 27,236 KiB |
| 1,024 | Candidate | 286,720 B | 2,078 | 393,888 B | 27,236 KiB |

The cache adds 8 bytes per object, 512 bytes at 64 objects and 8,192 bytes at 1,024. Allocation counts did not change. The process high-water RSS is too coarse to resolve this small difference. Instrumented counters establish affected work only; they do not establish latency. No detailed collision churn, broad-phase spatial skew, static/dynamic mix variation, dynamic hierarchy edit distribution or allocation-over-time timing comparison was collected.

## Initial capture preflight failure

Attempted command:

```sh
python3 benchmark/capture_transform_setter.py --baseline-build bin/transform-setter-baseline --candidate-build bin/transform-setter-candidate --baseline-work-build bin/transform-setter-work-baseline --candidate-work-build bin/transform-setter-work-candidate
```

The first invocation stopped at build preflight before launching a probe: its CMake cache parser looked up `CMAKE_BUILD_TYPE` while retaining the typed key `CMAKE_BUILD_TYPE:STRING`. The actual caches were matched Release builds (`-O3 -DNDEBUG`). The parser was corrected and checked directly. No timing sample was created by this invocation. After the user explicitly requested implementation of the converged tasks, the corrected capture below ran once; no automatic retry or matrix expansion was performed.

## Final bounded timing capture

Command:

```sh
python3 benchmark/capture_transform_setter.py --baseline-build bin/transform-setter-baseline --candidate-build bin/transform-setter-candidate --baseline-work-build bin/transform-setter-work-baseline --candidate-work-build bin/transform-setter-work-candidate
```

Status: **complete**. The bounded capture took 0.666 seconds and wrote 91,043 bytes to [pilot.raw.json](pilot.raw.json), within the existing 600-second and 50 MiB limits. It includes 15 matched workloads, three independent process repetitions per variant, five warmup and 30 measured frames for step cases, commands, build identities, raw timing samples, and separate work/resource results. Direct operation timings are medians of three process elapsed times. Step means are medians of the three per-process frame means; reported p95 is the median of each process's p95, not a pooled tail.

Final timing build identities: baseline source `c3f84644e8b9cb81a2a5fb1319fd0f76c54485965b1368320d60b7cc79334376`, binary `60ff11af23014c2d1b18760d302daf65324ecae69ed306a38ba4d992be991cd6`; candidate source `b5a379e06cfcb89b01292de3f8ab87e77e88126601cc128eb083666ea24a9a1d`, binary `86e78c3c293d899a61ca4281a775883a108f28fd5fb53f07e7f6e6db049e50fa`. Both use GCC 16.2.1, `Release`, `-O3 -DNDEBUG`, sanitizer none, debug tools off. The work builds share the same source aggregates; their binary hashes are listed above.

### Direct operations

Median elapsed time per process; lower is faster.

| Workload | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| 100k flat reads, 32 targets | 286.2 µs | 287.5 µs | +0.4% |
| 100k flat reads, 256 targets | 284.3 µs | 285.4 µs | +0.4% |
| 100k deep reads, depth 16 | 1,640.9 µs | 282.8 µs | -82.8% |
| 100k deep reads, depth 64 | 11,181.3 µs | 278.2 µs | -97.5% |
| 100k broad reads, 32 targets | 280.1 µs | 279.8 µs | -0.1% |
| 100k broad reads, 256 targets | 296.8 µs | 279.6 µs | -5.8% |
| 1,000 deep ancestor edits + 8 reads/edit | 933.6 µs | 262.9 µs | -71.8% |
| 10k unchanged box/circle/body setter iterations | 1,452.0 µs | 146.5 µs | -89.9% |
| Changed box/circle/body setter control | 1,427.7 µs | 1,498.9 µs | +5.0% |
| 10k unchanged velocity/angular setter pairs | 181.1 µs | 56.9 µs | -68.6% |
| Changed velocity/angular setter control | 200.0 µs | 212.9 µs | +6.5% |

The direct read gain grows with hierarchy depth; the flat controls remain effectively unchanged. Unchanged setter loops are much faster in this fixture. Changed-value setter controls are close to baseline and show small candidate slowdowns.

### Complete-step, setter-plus-step composite, and physics scopes

Each cell shows baseline → candidate median process mean, followed by the change. `p95` is the median of the three process p95 frame times.

| Workload | Baseline → candidate | Change | p95 baseline → candidate |
| --- | ---: | ---: | ---: |
| Complete step, 1 worker | 8.223 → 8.423 µs | +2.4% | 8.947 → 9.037 µs (+1.0%) |
| Complete step, 6 workers | 10.772 → 11.390 µs | +5.7% | 12.864 → 15.529 µs (+20.7%) |
| Setter-plus-step composite, unchanged, 1 worker | 10.345 → 8.850 µs | -14.4% | 10.911 → 9.368 µs (-14.1%) |
| Setter-plus-step composite, changed, 1 worker | 11.365 → 11.496 µs | +1.2% | 12.003 → 16.922 µs (+41.0%) |
| Setter-plus-step composite, unchanged, 6 workers | 13.528 → 12.054 µs | -10.9% | 19.027 → 17.243 µs (-9.4%) |
| Setter-plus-step composite, changed, 6 workers | 13.719 → 13.948 µs | +1.7% | 17.002 → 18.465 µs (+8.6%) |
| Physics integration/movement, 1 worker | 6.843 → 7.335 µs | +7.2% | 6.803 → 7.043 µs (+3.5%) |
| Physics integration/movement, 6 workers | 9.193 → 9.917 µs | +7.9% | 11.111 → 12.764 µs (+14.9%) |

The corrected physics timer covers only `PhysicsManager::applyPhysics(frameDelta)` and `applyMovement(frameDelta)`; the following `Runtime::simulateFrame(synchronizationDelta)` runs outside the clock. The setter-plus-step rows measure a **host-call composite**: 64 kinematic bodies receive two motion setters each, then `Runtime::simulateFrame` runs, all within one timer. The setters execute before the runtime call, not inside it. They are not samples of the runtime's complete-step boundary with in-step setter callbacks. Workload identities and final state checks matched between revisions. The historical raw samples and their source/build hashes remain unchanged; the probe's later label correction does not describe a new capture.

### Analysis and decision

The candidate has strong gains in the targeted deep-read and unchanged-setter operations. The read/edit workload also favors the candidate at the tested ratio of eight reads per ancestor edit. The setter-plus-step composite improves when assignments are unchanged; changed-value controls remain near baseline. These composite results do not isolate a complete step containing setter callbacks.

The no-setter complete-step medians are 2–6% slower, and the physics-only medians are 7–8% slower in the candidate. These scopes are very short, and three process repetitions show noticeable run-to-run variation. The six-worker p95 contrast is larger, so it remains a possible regression rather than evidence of a stable penalty. The synchronized immediate-position publication is in the subsequent, untimed `simulateFrame` and cannot directly account for time inside the physics-only interval; it could affect the separately measured full step or indirectly affect subsequent measurements. Within the physics interval, candidate `applyMovement` queues translated objects with `refreshImmediate=false`, and `queueTransform` evaluates that option without refreshing the subtree. Changed queue code and the additional eight bytes per object may affect instruction or memory behavior, but the retained samples do not isolate either effect. Baseline and candidate per-run mean ranges overlap: at one worker, 6.743–9.581 µs versus 6.871–7.610 µs; at six workers, 9.104–9.485 µs versus 7.464–9.984 µs. These ranges do not establish equivalence or identify a cause. The physics-scope regression remains unresolved.

Decision: retain the current implementation as a provisional candidate because the measured deep-read and unchanged-setter workloads improve substantially, and the setter-plus-step composite benefits from unchanged assignments in this fixture. Do not claim an overall game or multicore speedup: the plain complete-step and physics scopes move slightly slower, the physics-scope cause remains unresolved, and this fixture has no colliders or renderer. The scoped comparison covers only the recorded boundaries; the broader Constitution VI acceptance gate remains unsatisfied because this 3-repetition pilot is exploratory and true in-step setter work, collision churn, spatial skew, large live scene distributions, and broader workload tails remain uncovered. No additional matrix or automatic rerun was performed.

### Planned in-step setter workload

For a future explicitly requested bounded comparison, use the public `Runtime::simulateFrameWithCustomCallback` path to invoke the same 64-body unchanged and alternating changed velocity/angular-velocity assignments during the step, using an identical callback in both baseline and candidate. Time the complete step with its callback included, preserve the 5 warmup / 30 measured frames and three independent repetitions, and validate body-state and position checksums outside timing. Keep the same build flags, input, worker counts and existing 600-second / 50 MiB cap. Record the callback workload as a distinct case; do not relabel or replace the prior composite samples. No such workload or new performance result is claimed here.

## Sanitizer, artifacts and final status

`cleanup-asan` built the same seven selected targets. Default CTest execution failed 7/7 at process exit because LeakSanitizer cannot inspect threads under this environment's ptrace restriction; it reported no address or undefined-behavior diagnostic. Re-running with `ASAN_OPTIONS=detect_leaks=0` passed **7/7**. This checks AddressSanitizer and UBSan but does not validate leaks. TSan was not run. The evidence directory contains this report and the 91,043-byte raw pilot JSON; it is well below the 50 MiB artifact budget. No executable, library, build tree, source archive, image or per-frame identity list is retained here. Repository build directories remain under `bin/`. `git diff --check` and a trailing-whitespace check over all changed and untracked files passed.

Untracked feature documentation and benchmark sources are identified by the source-manifest aggregate and their repository paths; no single Git diff hash includes untracked files.

The candidate source comment in `src/gameobject.cpp` records the O(subtree) immediate-update cost, its reason, and batched subtree publication as concrete follow-up if edit-heavy timing regresses. This satisfies the local `EFFICIENCY:` documentation rule, but it does not waive the remaining evidence gaps and unresolved physics-scope regression.
