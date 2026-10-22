# SDL update benchmark reference

SDL 3.4.18, SDL_mixer 3.2.4, SDL_image 3.4.8, SDL_ttf 3.2.2; base vendor commit 9c4f3f6 plus build-integration and runtime-version metadata changes. No engine hot-path behavior changed.

36 invariant processes and 180 timing processes completed. All 36 final physics snapshots agree with the prior reference within existing tolerances. Five fresh processes per timing case, pinned Release, seed 0, dt=1/120, 120 warmup and 600 measured steps. Actual runtime versions are checked in every manifest.

Complete-step ms, mean ± sample standard deviation of process means:

| Workload (largest size) | Old 1 worker | New 1 worker | Old 6 workers | New 6 workers |
| --- | ---: | ---: | ---: | ---: |
| Broad lanes (1,000) | 0.608 ± 0.057 | 0.601 ± 0.039 | 0.208 ± 0.010 | 0.201 ± 0.005 |
| Dense broad (300) | 2.653 ± 0.017 | 2.659 ± 0.032 | 2.479 ± 0.034 | 2.533 ± 0.069 |
| Narrow elements (200) | 34.114 ± 0.861 | 34.907 ± 0.620 | 17.365 ± 0.588 | 17.551 ± 0.300 |
| Rigid container (1,000) | 1.513 ± 0.023 | 1.544 ± 0.026 | 1.167 ± 0.030 | 1.206 ± 0.036 |
| Static churn (1,000) | 2.735 ± 0.054 | 2.896 ± 0.123 | 1.401 ± 0.032 | 1.441 ± 0.016 |
| Moving churn (1,000) | 2.845 ± 0.046 | 2.893 ± 0.113 | 1.437 ± 0.042 | 1.463 ± 0.031 |

[Full validation and sanitizer findings](../../../docs/SDL_UPDATE_VALIDATION.md). The tested APIs/SDK are compatible, but the update is not sanitizer-clean: WAV arithmetic fix was replaced, FluidSynth leaks persist, and desktop TSan reports remain.

- [36 new aggregates](summary.csv), [old/new comparison with spread](comparison.csv), [capture context/order](capture.json), [invariants/tolerances](invariants.json).
- runs/*.tar.gz preserves all 180 summaries/raw samples/manifests/logs; validation/*.tar.gz preserves the 36 oracle runs and full final snapshots.
- [Paired churn control](paired-churn/summary.json): historical single-worker static churn rose 5.9%; interleaved old/new runtime comparison measured +1.76% with overlapping run ranges. No isolated library slowdown is established.
- [Source snapshot](source.tar.gz), [checksums](SHA256SUMS). The [previous reference](../cpp-cleanup/RESULTS.md) remains byte-for-byte unchanged.

Keep complete-step clocks separate from old partial frame/host-step metrics and Godot monitors. Headless timings do not measure hardware rendering/audio throughput. No sanitizer failure was suppressed to obtain these results.
