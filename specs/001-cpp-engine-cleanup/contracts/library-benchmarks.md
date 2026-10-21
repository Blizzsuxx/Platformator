# Contract: Library, Assets, Validation, and Benchmark Output

## Library/package boundary

Preserve target `Platformator::platformator`, `include/platformator/` as the public surface, a static core, the stock runner, and the separate Mario consumer. Source consumers use add_subdirectory; installed consumers use find_package with an exported relocatable target and represented transitive dependencies.

Bundled SDL/Eigen/TBB/JSON consumption remains available without network downloads. Use bundled dependencies' supported install/export rules; do not substitute a machine-specific list of absolute library paths. Public headers include their own requirements and compile independently. Stop generating unused private forwarding wrappers; remove old shims only after normalizing includes and proving no public consumer needs them. Preserve private implementation headers where internal tests genuinely require them.

Default builds do not register dependency tests/examples that are not part of the requested build. Add `PLATFORMATOR_SANITIZER=none|address-undefined|thread`; unsupported compiler/configuration combinations fail at configure time. Address/UBSan instrument the engine and test code. The thread configuration also builds TBB with compatible ThreadSanitizer support, or reports the specific environment limitation; broad race suppressions do not count as passing validation.

The supported runtime remains main-thread mutation, one active instance, runtime-owned SDL setup/teardown. Physics-only construction skips both video and audio devices. Default desktop construction initializes only configured backend features and throws if required initialization fails.

## Assets and I/O

Add `RuntimeOptions.assetRoot` and the corresponding `--asset-root` runner argument. It identifies the assets directory itself. Normalize the explicit root once at startup; relative roots are resolved against startup cwd once. If omitted, packaged layout resolution uses the executable directory's assets folder, then its install-prefix assets folder, in that order, without upward searches or requiring default.scene/audio/animation directories. The source runner/README commands pass the repository root explicitly where necessary.

For an asset field, logical `assets/textures/Foo.png` resolves as `<assetRoot>/textures/Foo.png`; `textures/Foo.png` is also relative to assetRoot. Preserve case. Reject paths escaping the configured asset root. Scene files accept absolute paths, `assets/...` paths relative to assetRoot, and other relative paths resolved once against the host-supplied/startup base. Serialization emits canonical logical assets paths without lowercasing disk names. Supported legacy scene-relative asset spellings are normalized during load with the scene file as context and must resolve within the configured root.

The example packages its own scene and all required engine fallback assets deliberately. It must run from an unrelated working directory and after prefix relocation. A core build must not acquire required assets accidentally from building an example first. Package layout has executable(s) under bin, runtime libraries under lib, headers under include, package config under lib/cmake/Platformator, and resources under assets.

Missing optional assets may use an explicitly documented fallback and emit one cold-path diagnostic. Missing required behavior references/resources or missing fallback assets are load/startup errors; a fallback must not silently turn invalid scene input into a success. Preserve the old scene on preparation errors. Save writes a sibling temporary file, checks write/close success, then replaces the destination with the supported platform operation; cleanup leaves the prior successful destination intact on failure.

## Validation boundary

Use project CTest names prefixed `platformator_` and labels including lifecycle, scene, reference, transform, physics, and package where applicable. Test code may favor clear traces, heap allocation, scans, and brute-force geometric checks. Existing invalid helper behavior is repaired before interpreting failures, and known engine regressions remain visible until fixed.

At minimum cover normal/sanitized lifecycle, a public-header-only host, serial/multiworker physical invariants, allocation/queue-work complexity, and clean source/installed/relocated consumers. Package tests launch a finite smoke mode in the example (`--smoke-steps N`) so CI does not rely on killing an endless event loop; it uses dummy SDL drivers on headless machines. This is a planned runner/example validation argument, not an existing guarantee.

## Benchmark CLI and metrics

Keep current scenario/workload flags and extend them with `--workers N` and `--seed N`. Worker limits are set once in the benchmark process; they do not imply general concurrent host mutation. Keep --warmup-frames, --measure-frames and --dt compatibility, while documenting that these count simulation steps. A rendered benchmark stops on quit rather than counting skipped attempts.

CSV output schema version 2 distinguishes at least:

| Name | Included work |
| --- | --- |
| `complete_step` | Start/hook, fixedUpdate, synchronization, physics, events, update/animation/lateUpdate, structural/scene commit, destruction, transient work, final cleanup |
| `physics` | Physics preparation, broad/narrow phase, support reduction, force/solve/movement work; declared nested subscopes |
| `cleanup` | Deferred reconciliation, exits, retirement/destruction, final drains |
| `scene_transition` | Preparation/commit when a step requests it; part of complete_step |
| `render` | Render submission/presentation when requested; not included in simulation complete_step |

Do not silently relabel old `frame` measurements as equivalent. Plot/sweep parsers reject unknown/missing schema or required metrics instead of substituting zero. Report nested scopes honestly; they need not sum when work intentionally overlaps/nests.

Run metadata is written outside the step loop: commit plus dirty status, compiler/flags, Debug/Release/instrumentation, machine/OS, dependency versions, scenario parameters, seed, dt, workers, warmup, measured count, repetitions, timing schema/boundaries, and rendering policy. Reuse reserved sample/scratch capacity.

Extend the sweep tool with `--repetitions N` (default five), `--workers`, `--seed`, and `--raw-output-dir`; preserve one raw CSV/manifest per population/repetition plus an aggregate reporting run-to-run median/spread and per-run distribution statistics. Invariant-checking validation runs are labeled and separated from uninstrumented Release performance runs. No new statistical plotting dependency is required for this cleanup.

Godot comparisons are published only with a documented equivalent workload and measurement boundary. If matching cannot be established, report the scopes separately and explain their meaning. Existing graphs remain historical artifacts. No numeric improvement or regression percentage is claimed before a corrected workload is measured repeatedly on a pinned machine.

## Efficiency review rule

For each runtime change, identify its repeated work and allocation behavior before selecting the implementation. If an efficient design is feasible, implement it rather than leaving a justification comment. If a less efficient path must remain, put this information at that code, beginning with `EFFICIENCY:`:

1. The slower operation and its frequency/complexity.
2. The concrete reason it must remain now.
3. The efficient alternative.
4. The exact prerequisite/work needed to replace it.

A vague TODO, “for simplicity,” or “optimize later” is insufficient. Tests are exempt from runtime complexity targets. Validation-only instrumentation must be labeled so its overhead is not presented as engine performance.
