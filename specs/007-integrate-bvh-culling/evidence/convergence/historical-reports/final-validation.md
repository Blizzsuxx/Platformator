# Final validation record (T080)

Final implementation source fingerprint:
`42d3e64f95876cb9fa0284bbf36b95a2e7e755692a1158ecc072e7f38ecec9dc`.
All build/test runs below used the current source. Rebuild and test logs are
retained under [`final-validation/`](final-validation/).

## Build and test results

| Configuration | Result | Evidence |
|---|---|---|
| Release full CTest | 55 tests configured; all tests that ran passed. Seven work-probe cases were skipped because the Release timing build disables probes. | [`ctest-final.log`](final-validation/ctest-final.log) |
| Probes-enabled Release spatial suite | 17/17 passed, including runtime no-work probes for one and six workers and every renderer choice. | [`build-work-final.log`](final-validation/build-work-final.log), [`ctest-work-spatial-final.log`](final-validation/ctest-work-spatial-final.log) |
| ASan/UBSan spatial suite | 11 tests passed; six probes-only cases were skipped in this configuration. LeakSanitizer was disabled with `ASAN_OPTIONS=detect_leaks=0` because LSan aborts under the environment's ptrace restrictions. | [`build-asan-final.log`](final-validation/build-asan-final.log), [`ctest-asan-final.log`](final-validation/ctest-asan-final.log), prior LSan attempt: [`ctest-asan.log`](final-validation/ctest-asan.log) |
| TSan spatial suite | 11 tests passed; six probes-only cases were skipped. The run used only the retained SDL audio symbol suppressions. | [`build-tsan-final.log`](final-validation/build-tsan-final.log), [`ctest-tsan-spatial-final.log`](final-validation/ctest-tsan-spatial-final.log), [`tsan-sdl-audio.supp`](final-validation/tsan-sdl-audio.supp) |
| Standalone spatial sweep contracts | 17/17 passed. | [`python-spatial-sweep-final.log`](final-validation/python-spatial-sweep-final.log) |
| Standalone schema-4 benchmark contracts | 7/7 passed. | [`python-spatial-benchmarks-final.log`](final-validation/python-spatial-benchmarks-final.log) |
| Benchmark Python unit tests | 10/10 passed. | [`python-benchmark-tests-final.log`](final-validation/python-benchmark-tests-final.log) |
| Vendored Box2D source checksums | All 20 retained upstream files passed `UPSTREAM.sha256`. | [`vendor-checksums-final.log`](final-validation/vendor-checksums-final.log) |

The full Release CTest run includes default regressions, spatial/pair/physics
tests, image/order/lifecycle tests, runner/schema contracts, the retained
Box2D tree test, and installed/relocated package smoke. The package smoke
passed in 29.61 seconds and exercised public headers, the installed archive,
source and installed consumers, and relocation. No Box2D public header or
client package dependency was introduced.

The TSan suite is clean with the documented suppression. An unfiltered run
reported two races inside SDL 3.4.18 audio conversion/playback functions
`SDL_SetAudioStreamFormat_REAL` and `SDL_PlaybackAudioThreadIterate`, rather
than spatial-index code; the unfiltered output is retained in
[`ctest-tsan-spatial.log`](final-validation/ctest-tsan-spatial.log), and the
suppression names only those external symbols. The selected spatial, physics,
geometry, renderer image, and lifecycle tests pass with that narrow suppression.

## Binary and evidence integrity

The final `nm -C` audit found no defined allocation interceptors in
`platformator_benchmark_runner`; the resource runner defines the expected C++
allocation hooks. All six final validation/timing/resource roots pass their
`checksums.sha256` files. All 1,932 timing, validation, and resource eligibility
records carry the final source fingerprint. See
[`final-evidence-integrity.log`](final-validation/final-evidence-integrity.log).

The final evaluation includes 248/248 replay validations, 28/28 engine
validations, 1,240/1,240 eligible replay timing processes, 140/140 eligible
engine timing processes, 248/248 linked replay resource captures, and 28/28
linked engine resource captures. Each reported timing configuration has five
fresh processes. Resource captures are explicitly timing-ineligible.

## Limits

Hardware-renderer validation/performance, CPU-pinned timing, an external host
contention monitor, and a full live-engine population matrix through 10,000
objects were unavailable. The 10,000-object coverage is replay-based. The
software renderer was used for actual SDL captures. Resource accounting reports
process peak RSS and selected owner capacities, while direct C/SDL/oneTBB
allocations and allocator metadata remain excluded or unclassified as described
in the resource manifests. These are evidence limits, not failed checks.
