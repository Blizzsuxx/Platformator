# Final implementation validation

## Correctness, package, and worker checks

- Full Release/work-probe build: `cmake --build build/009-incremental-validation -j4` completed successfully.
- Full CTest: `ctest --test-dir build/009-incremental-validation --output-on-failure -j4` passed all 37 tests. This includes the public consumer/package smoke, render geometry/order/lifecycle tests, worker publication, and allocation-observer self-test.
- Focused comparison: `platformator_render_culling_test`, `platformator_render_culling_index_test`, `platformator_render_culling_worker_test`, and `platformator_runtime_recreation_test` passed in the Release/work-probe build.
- ASan/UBSan targets were built in `build/009-asan` with `PLATFORMATOR_SANITIZER=address-undefined`. The lifecycle, index, and rendered transition tests passed with `ASAN_OPTIONS=detect_leaks=0`.
- LeakSanitizer itself is unavailable in this container execution context: the initial run with default leak detection ended with `LeakSanitizer has encountered a fatal error` and its runtime message that it does not work under ptrace. No leak-detection result is claimed.
- TSan build: `cmake -S . -B build/009-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPLATFORMATOR_SANITIZER=thread -DPLATFORMATOR_ENABLE_WORK_PROBES=ON -DPLATFORMATOR_ENABLE_BENCHMARKS=ON -DPLATFORMATOR_BUILD_TESTS=ON -DPLATFORMATOR_BUILD_EXAMPLES=OFF -DPLATFORMATOR_BUILD_RUNNER=OFF -DPLATFORMATOR_INSTALL=OFF`, followed by building `platformator_render_culling_worker_test` and `platformator_linear_render_probe`.
- The unsuppressed worker TSan test reported a race in SDL's dummy audio playback thread (`SDL_PlaybackAudioThreadIterate`) started by the unrelated mixer device during window initialization. The allocation-observer self-test passed without suppression. The worker test then passed with only `race:SDL_PlaybackAudioThreadIterate` suppressed via `/tmp/009-tsan-sdl-audio.supp`; engine, physics, and publication reports remain unsuppressed. This is a dependency-specific caveat, not a clean unsuppressed TSan result.
- The Release worker regression runs at one and six workers and compares same-frame images, ordered identities, physics transforms, and collider vertices. The physics/render work assertions passed at both worker counts.

## Review scope

The render cache is owned by `SDLWindow`, and records stay dense and rank-aligned with the existing Sprite slots. Handles are validated by the existing runtime/generation mechanism. Dirty notifications are owner-thread queued and coalesced; physics worker results are published after the TBB join. Grid state exists only for indexed mode. Successful scene replacement releases the old high-water storage after old registrations retire; failed preparation preserves the current scene/cache. The direct Camera render API retains submit-all behavior, and unsupported SDL mappings suspend cached selection while preserving queued changes.

The spatial range arithmetic checks ordered signed cell bounds before unsigned span calculation. Capacity multiplication is guarded before reserve. SDL camera geometry remains shared through the existing preparation path. The renderer index is separate from colliders and does not add physics queries or public strategy APIs.

The allocation observer counts C++ `new`/`new[]` only; it does not attribute aligned, C, SDL, renderer-driver, or OS allocations. The full acceptance inventory and SC-006/SC-007 remain open; the focused pilot below is exploratory.

`git diff --check` passed during review. Arithmetic checks guard cell-range spans and capacity multiplications before allocation; the review found no stale `EFFICIENCY:` comments, duplicate SDL geometry classifier, collider coupling, or population-wide order repair in the maintained selection path.

## Corrective focused checks and pilot

After the published-world-transform, textureless side-list, dynamic-profile-dispatch, bounded proof-memory, setup/growth/reset attribution, and batched visibility-order changes, the Release work-probe validation target and Release timing probe were rebuilt from the same source inputs. Both embed candidate source `5a500fe96e6cb99acc9af6e81c60f61710380c0e1d14362e84d602d9bb53dcf8`, fixture `170c6203834cd331174fdd004ffda0f23d1508bb1c722599266b6759337b5eb6`, and harness `bd85667c67f3a7b0743de854bc248cb1d1f64e6c0beb0d94d360d9898d2976dd`. Validation is Release/work-probe-on; timing is Release/work-probe-off; both disable debug tools and sanitizers. Exact identities, source manifests, copied probes, and build logs are in the durable run root indexed by [capture-index.md](capture-index.md).

The focused CTest selection passed 7/7, including geometry, cache/render, worker, trajectory, contract/package, index, and benchmark CLI tests. `python3 -m unittest benchmark.tests.test_linear_render` passed 34/34; `py_compile` passed. The new memory regression validated 4,000 streamed proof frames with under 2 MB peak verifier memory and no retained per-frame array.

The exploratory capture at `benchmark/artifacts/linear-render-culling/009-20261029-run-01/captures/pilot-final/` completed six selected cases, 54 timing processes, and 24 resource proofs. The relative-path verifier recomputed CSV/raw averages, paired sample means, process aggregates/differences, resource links, checksums, and probe identities. It also passed against a temporary relocated copy of only this focused pilot subset with its indexed binaries and runtime libraries. See [RESULTS.md](RESULTS.md) for complete scope, performance direction, and limits.

Three prior pilot attempts stopped before writing a success report because the capture verifier initially read manifest fixture/harness fields from the wrong location and then omitted a required case key in a shared statistics check. Those directories are preserved under the bundle's `captures/`; none are analyzed as successful captures. The final attempt used corrected validators and matching source/build identities. No campaign or capture subprocess is currently running.

The pilot has only three independent processes and 30 measured steps per case, uses an unpinned software renderer, omits per-frame selected-case parity and frozen-reference/stock controls, and does not include several full-matrix workloads. It suggests cache gains on mostly-offscreen, pan, and small-change cases, while exposing a fully moving regression; the index's additional sparse/pan reductions are small and need more independent runs. Do not close T040–T046, SC-006, or SC-007 from this evidence. Existing ASan/UBSan and SDL audio TSan caveats above remain the recorded sanitizer results.
