# SDL update validation

The engine's used C++ APIs remain compatible: normal/Release tests, public headers, Mario, source consumers, installed SDK consumers and relocated consumers pass. The original update replaced the local WAV arithmetic fix; it has since been restored as described below. The FluidSynth leak and broader desktop TSan dependency diagnostics remain unresolved.

## Local WAV fix restored (2026-10-23)

Restored the existing local patch in SDL3_mixer 3.2.4: `FetchPCM24LE` multiplies the signed high byte by 65536 instead of shifting a potentially negative signed value left by 16. The multiplication fits in Sint32 for the entire signed-byte range. The altered line is marked as a local patch; no upstream contribution was made.

Rebuilt `bin/cleanup-asan` and ran the full non-package suite outside the sandbox with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`: **21 passed, 0 failed, 2 intentionally skipped**, 12.17 s. Both previously failing audio tests pass. The [complete follow-up log](sdl-update-evidence/wav-fix-asan-tests.txt.gz) is retained. Leak detection was disabled for this memory-access/undefined-behavior check; existing leak/TSan findings are not claimed fixed.

The remaining sections and benchmark captures record the original dependency-update validation before this restoration. Their archived sources, fingerprints and results are preserved; benchmarks were not recaptured for this decoder arithmetic patch.

## Versions and integration

SDL **3.4.2 → 3.4.18**, mixer **3.2.0 → 3.2.4**, image **3.4.0 → 3.4.8**, ttf **3.1.0 → 3.2.2**. User vendor commit: `9c4f3f68cec11f68e35ab74b79906e26733a88fd`; initial working tree clean.

CMake still named the removed old vendor folders. Updated those paths and the WAV fingerprint/source-archive path. Benchmark metadata now calls the linked libraries' version functions instead of recording stale literal versions. No engine runtime behavior/vendor source was changed and no sanitizer suppression was added.

## Tests

Actual counts (CTest's percentage includes skips):

| Configuration | Passed | Failed | Skipped | Time |
| --- | ---: | ---: | ---: | ---: |
| Normal Debug, non-package | 21 | 0 | 2 | 8.06 s |
| Release, benchmarks enabled | 22 | 0 | 1 | 4.04 s |
| Strict ASan/UBSan, leaks off | 19 | 2 | 2 | 15.72 s |
| Leak-enabled ASan, UBSan continues reporting | 7 | 14 | 2 | 21.89 s |
| Instrumented TBB/TSan physics | 3 | 0 | 0 | 7.59 s |
| Broader TSan non-package suite | 18 | 3 | 2 | 23.16 s |
| Dedicated work/allocation oracle | 1 | 0 | 0 | 0.03 s |

Normal/Release/sanitizer suites include the 1,000-transition stress host. Normal build includes Mario and independent public-header translation units. Package smoke also passes: core-only SDK build/install, source add_subdirectory consumer, installed and relocated public-only consumers, installed-SDK Mario, and relocated installed Mario, all from unrelated working directories. Package smoke uses separate Release builds and is excluded from sanitizer claims. Dedicated work checks still verify zero counted warmed idle C++ allocations and affected-only transform/body work.

### Replaced WAV fix

Strict UBSan fails `platformator_regression_tests` and `platformator_scene_script_roundtrip_test` at `vendored/SDL3_mixer-3.2.4/src/decoder_wav.c:789`: **left shift of negative value -8**, reading the valid existing 24-bit jump WAV. The local 3.2.0 copy had an arithmetic fix; 3.2.4 was imported without it and still contains the signed shift. Restoring that fix is necessary to recover the previous strict sanitizer gate. Normal tests passing does not make this operation defined.

### Leaks persist without engine code

All 14 leak-failing processes have allocation stacks through `LoadModule_fluidsynth → FLUIDSYNTH_init → MIX_Init`: **8,200 bytes / 164 allocations**, the same prior signature. No engine-owned allocation leak or alternative allocation path was reported. This is not a universal guarantee of leak freedom.

The [standalone probe](sdl-update-evidence/mixer-probe.cpp) links only instrumented SDL3/SDL3_mixer. It prints actual runtime versions SDL `3004018`, mixer `3002004`, then performs init/quit cycles:

| Cycles | Exit | Leak report |
| --- | ---: | --- |
| 0 | 0 | None |
| 1 | 1 | 200 bytes / 4 allocations |
| 10 | 1 | 2,000 bytes / 40 allocations |

This reproduces the dependency initialization/unloading leak without Platformator. No MIDI support was removed to hide it. Leak scans run outside the sandbox with `ASAN_OPTIONS=detect_leaks=1`. The leak-only suite uses `UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1` to retain the WAV diagnostics while letting every leak scan finish; the separate strict UBSan suite uses `halt_on_error=1` and remains failing.

### Broader desktop TSan findings

A first foundation host passed, but the full suite reported:

- `platformator_regression_tests`: main-thread `SetTrackOutputStreamFormat` / `MIX_SetTrackAudio` writes `track->output_spec` while the audio-thread `MixerCallback` reads it. Both accesses are in SDL_mixer 3.2.4. Its public header documents MIX_SetTrackAudio as safe from any thread. The previously documented class of desktop audio concurrency issue remains observable.
- `platformator_scene_transition_contract_test` and `platformator_runtime_step_test`: potential lock-order inversions in system `libdbus-1.so.3` across SDL startup/shutdown. Normal tests did not deadlock. These warnings require investigation; the earlier matrix did not establish a clean full desktop TSan baseline, so they cannot be dated as new regressions from this upgrade alone.

Physics-only worker tests remain clean with both TBB and engine instrumented. No reports were suppressed.

## Benchmarks

**36 invariant processes and 180 fresh pinned Release timing processes completed.** All 36 final old/new body/contact snapshots agree within the pre-existing tolerances: identities/support/sleep/features exact, other physical fields within declared floating-point bounds. No physical assertion/tolerance was relaxed. All 180 raw runs and 36 aggregates were reparsed; sample counts, means/spread, source fingerprint and actual updated runtime versions match.

Complete-step milliseconds below are mean ± sample standard deviation across five independent process means. Complete-step includes final cleanup/destruction; rendering, startup and recorder storage are excluded.

| Workload (largest size) | Old 1 worker | New 1 worker | Old 6 workers | New 6 workers |
| --- | ---: | ---: | ---: | ---: |
| Broad lanes (1,000) | 0.608 ± 0.057 | 0.601 ± 0.039 | 0.208 ± 0.010 | 0.201 ± 0.005 |
| Dense broad (300) | 2.653 ± 0.017 | 2.659 ± 0.032 | 2.479 ± 0.034 | 2.533 ± 0.069 |
| Narrow elements (200) | 34.114 ± 0.861 | 34.907 ± 0.620 | 17.365 ± 0.588 | 17.551 ± 0.300 |
| Rigid container (1,000) | 1.513 ± 0.023 | 1.544 ± 0.026 | 1.167 ± 0.030 | 1.206 ± 0.036 |
| Static churn (1,000) | 2.735 ± 0.054 | 2.896 ± 0.123 | 1.401 ± 0.032 | 1.441 ± 0.016 |
| Moving churn (1,000) | 2.845 ± 0.046 | 2.893 ± 0.113 | 1.437 ± 0.042 | 1.463 ± 0.031 |

Largest-case historical changes range from -3.2% to +5.9%. Host conditions differ, so these are observations, not isolated library speed claims. Static 1,000-churn/one-worker lies above its old historical range and received an interleaved control: same current runner/new headers, five old-library and five new-library processes, runtime versions verified, retained Release shared objects selected with LD_LIBRARY_PATH.

Control: **old 2.838 ± 0.208 ms; new 2.888 ± 0.153 ms** (+1.76%), with overlapping ranges (old 2.654–3.190; new 2.729–3.089). This does not establish a clearly separated 5.9% library slowdown. It is a runtime-library control using retained binaries, not a clean source rebuild of the old dependency tree. Hashes, process order and raw results are retained.

Main capture: 2026-10-22T10:00:17Z–2026-10-22T10:05:44Z; Ryzen 5 7535HS, GCC 16.2.1, Linux 6.18.49-1-MANJARO, Release -O3 -DNDEBUG, no LTO/sanitizer/debug/work probes. One worker pinned CPU 0; six to 0,2,4,6,8,10. Seed 0, dt=1/120, 120 warmup, 600 measured steps, five repetitions, shuffle seed 1, CPU 0 performance governor. Builds/tests finished before timing. Background load/frequency/temperature are not fully controlled. Load averages before: [4.37158203125, 10.6005859375, 8.3515625]; after: [4.42626953125, 6.4365234375, 7.15673828125].

Source fingerprint: `6b755cd07b6f0dbdf88100aa29d5f639717c82a645dc0bd20d1741f8787de2e1`. The extracted snapshot matches the binary/invariant/timing metadata. Vendor sources are anchored by commit 9c4f3f6; the WAV file is archived explicitly.

## Evidence and reproduction

[New benchmark reference](../benchmark/results/sdl-update-9c4f3f6/RESULTS.md), [36-row comparison](../benchmark/results/sdl-update-9c4f3f6/comparison.csv), [paired control](../benchmark/results/sdl-update-9c4f3f6/paired-churn/summary.json), [per-test summary](sdl-update-evidence/test-summary.json). Complete compressed configure/build/test/sanitizer/package/probe logs are in [sdl-update-evidence](sdl-update-evidence), alongside the reproducer and verification/control drivers. Previous benchmark files/checksums remain unchanged.

```sh
cmake --preset cleanup-debug -DPLATFORMATOR_BUILD_EXAMPLES=ON
cmake --build bin/cleanup-debug --parallel 3
ctest --test-dir bin/cleanup-debug --output-on-failure -E package_smoke
cmake --preset cleanup-asan
cmake --build bin/cleanup-asan --parallel 3
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure -E package_smoke
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1 \
  ctest --test-dir bin/cleanup-asan --output-on-failure -E package_smoke
```

Sanitizers ran outside the sandbox (LeakSanitizer still encounters ptrace restrictions inside). Benchmark reproduction uses capture_cleanup.py --mode validate followed by --mode time with a new output directory, after all other builds/tests finish. Full TSan command: `TSAN_OPTIONS=halt_on_error=1:exitcode=66 ctest --test-dir bin/cleanup-tsan --output-on-failure -E package_smoke`. No compatibility is claimed for unused SDL APIs, untested operating systems or hardware renderers. No commit was made.
