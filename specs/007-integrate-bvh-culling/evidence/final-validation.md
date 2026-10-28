# Final convergence validation

Original T083–T093 verification source: `f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1`. Those suite logs remain retained under [convergence/verification](convergence/verification/). The T094–T097 final-source fingerprint is `a8795fcece092b3dd47b0ddc4b736015b2c4215500f85472ec5ccba309c54025`; its targeted validation is recorded below.

| Check | Result | Evidence |
|---|---|---|
| Full Release CTest | 55 configured; all 48 runnable tests passed, seven probes-only cases skipped | [release-ctest-final.log](convergence/verification/release-ctest-final.log) |
| Probes-enabled spatial suite | 17/17 passed | [work-spatial-final.log](convergence/verification/work-spatial-final.log) |
| ASan/UBSan spatial suite | 11 runnable tests passed; six probes-only cases skipped | [asan-spatial-final.log](convergence/verification/asan-spatial-final.log) |
| TSan spatial suite | 11 runnable tests passed; six probes-only cases skipped | [tsan-spatial-final.log](convergence/verification/tsan-spatial-final.log) |
| Installed/relocated package and source consumers | Passed in 27.27 seconds as part of full CTest; no public Box2D header/client dependency | Release CTest log |
| Production edge checks | 10/10 passed: uniform zoom skips static bounds; multi-cell migration retains a pair; filter rejection/restoration and generation reuse publish exact transitions at one/six workers | [edge-cases-final.log](convergence/verification/edge-cases-final.log), [reproduction script](convergence/verify_edge_cases.py) |
| Actual image/order fixtures | Passed; current PPMs and metadata retained | [image log](convergence/verification/render-images-current.log), [render-images](convergence/render-images/) |
| Upstream Box2D checksums | All 20 retained files passed | [vendor-checksums.log](convergence/verification/vendor-checksums.log) |
| Allocation interceptor separation | Timing runner defines no new/delete hooks; resource runner defines expected hooks | [binary-hooks-final.txt](convergence/verification/binary-hooks-final.txt) |
| Original matched evidence integrity | All 19,201 per-bundle checksum entries verified, 1,107,885,683 bytes; original records matched their recorded source and qualification keys | [INTEGRITY.json](convergence/INTEGRITY.json), [retained-bundle audit](qualification-audit.md) |

LeakSanitizer remains disabled with `ASAN_OPTIONS=detect_leaks=0` because of the environment's documented ptrace restriction. This is not a leak-detection pass. TSan uses only the retained narrow external SDL audio symbol suppressions for `SDL_SetAudioStreamFormat_REAL` and `SDL_PlaybackAudioThreadIterate`; it does not suppress spatial-index code. The suppression file is [retained here](final-validation/tsan-sdl-audio.supp).

The Release run includes runtime/lifecycle/physics regressions, adapter and retained upstream tree tests, production collision/replay oracles, renderer geometry/pixel/order tests, phase-boundary checks, source-fingerprint and strict schema/sweep/resource contracts, CLI selection and installed consumers. Final source changes were tested before timing collection; builds/tests/resources did not overlap timing sweeps.

The original bundles contain 720 validation processes, 3,600 timing processes and 464 resource processes. These remain tied to their historical source; the stricter follow-up audit records missing runtime scenario or replay operation metadata and does not reuse them for the corrected path.

The T094–T097 follow-up built the final Release runner and captured 64 validation, 320 five-process timing, and 64 resource processes. All passed the expanded qualification checks; resources pair to timing, and each actual-render validation passed exact same-frame pixel/order checks over 720 frames. Replay tests passed in cleanup-work and ASan/UBSan configurations; the probes-enabled linear test passed in cleanup-work. LeakSanitizer was disabled under the environment's ptrace restriction. Hardware-renderer performance, pinned-core measurement and an external contention monitor remain unverified; see [MACHINE.md](convergence/MACHINE.md), [RESULTS.md](RESULTS.md), and [linear-correction.md](linear-correction.md).
