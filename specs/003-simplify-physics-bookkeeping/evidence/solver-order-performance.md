# Performance after solver-order removal — 2026-10-26

The heavier 1,000-body workload improves about 6–8% in three measured configurations. The short one-worker result is less conclusive, and the small one-worker case has a higher mean. This is a workload-specific engine comparison, not a universal speedup or isolated sort-cost measurement.

## Comparable setup

The sorted Release runner was preserved before rebuilding; both sorted and unsorted runners now have separate immutable copies of their identical vendored shared libraries. Independent ldd output confirms those copies are loaded through stage-specific LD_LIBRARY_PATH. Original sorted source fingerprint b87bc7a... and new unsorted cc6ce045... are recorded in full in the [machine-readable results](solver-order-performance.json). The sorted source archive is retained under bin/physics-bookkeeping-20261025; a fresh unsorted source archive and runner/library hashes are in the raw root. Source inspection found only comments/formatting/braces in body indexes beyond the three runtime files involved in removing sorting; no other algorithm change was introduced for the measurement.

Both builds use the same compiler/Release flags/dependency versions, seed 0, dt 1/120, 120 warmup steps, headless setup and original schema-3 timing boundaries. One worker uses CPU0; six use CPUs0,2,4,6,8,10. All captures are unprofiled, without sanitizer/work/debug instrumentation. No deliberate competing builds/tests/probes ran during timing. Five independent processes per stage/group, shuffled with seed27 and alternating stage order: 70 primary timing processes plus 20 targeted follow-ups. The original 600-step window and extended 6,000-step trajectory are reported separately. Tails below are means of per-process quantiles; all raw samples, maxima, process means, SD/range, paired differences and eight scopes are retained. No run is discarded and overlapping ranges are not proof of equivalence.

Raw root: `bin/solver-order-performance-20261026-lhc0r4t3`. [Capture reproducer](measure_solver_order.py). Schedules, exact commands, per-phase checksums, manifests, source snapshot, isolated binaries and logs are retained there. Four separate per-step-oracle runs (1,000 bodies, 600 steps, both worker counts and stages) pass physical validity; there is no old/new or cross-worker trajectory equality assertion after the approved contract change. Strict readers, provenance/build/workload equality and raw checksums verify all 94 processes.

The working physicsmanager.cpp was reformatted while captures ran (lambda layout and braces around existing single-statement branches/loops). The isolated runners and archived measured source remained unchanged. These results refer to those frozen binaries; the formatting-only observation and differing source fingerprint are retained in post-capture-source-observation.json. No user edit was reverted.

## Primary full-step results

| Workload | Workers | Measured steps | Sorted ms | Unsorted ms | Mean change | Sorted / unsorted p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broad_phase (100) | 6 | 600 | 0.035203 | 0.034319 | -2.51% | 0.044384 / 0.043723 |
| rigid_body_container (100) | 1 | 600 | 0.071289 | 0.075405 | +5.77% | 0.127532 / 0.141416 |
| rigid_body_container (100) | 6 | 600 | 0.065214 | 0.064822 | -0.60% | 0.123985 / 0.122452 |
| rigid_body_container (1000) | 1 | 600 | 1.541187 | 1.540450 | -0.05% | 2.091346 / 2.291047 |
| rigid_body_container (1000) | 1 | 6000 | 1.942339 | 1.813858 | -6.61% | 2.490323 / 2.276760 |
| rigid_body_container (1000) | 6 | 600 | 1.233595 | 1.134067 | -8.07% | 1.856069 / 1.589263 |
| rigid_body_container (1000) | 6 | 6000 | 1.570706 | 1.482807 | -5.60% | 1.987953 / 1.866647 |

Negative change means faster. Larger population/longer-window gains are observed effects of the whole amended runtime. They are not direct wall-time deductions from the earlier 7%/4% sampled CPU-cycle shares.

## Targeted repeats of the uncertain one-worker results

| Workload, 600 measured steps | Primary change | Fresh follow-up sorted ms | Unsorted ms | Follow-up change |
| --- | ---: | ---: | ---: | ---: |
| rigid_body_container-n100-w1-steps600 | +5.77% | 0.069840 | 0.070628 | +1.13% |
| rigid_body_container-n1000-w1-steps600 | -0.05% | 1.502097 | 1.454370 | -3.18% |

The 100-body/one-worker downside persists at a smaller magnitude in follow-up (+5.77% then +1.13%). Its primary unsorted means span 0.07117–0.08760 ms, while sorted spans 0.06762–0.07413 ms; every observation remains in the reported mean. The short 1,000-body/one-worker result is -0.05% initially and -3.18% in follow-up, so a precise repeatable gain is not established there. The zero-contact control changes -2.51% with wide overlapping run-mean ranges and no solver contacts: do not attribute that difference to sorting or present it as a solver speedup. Host/binary-layout effects are not isolated.

## Why the savings vary

Removing canonical solve order changes the numerical trajectory and therefore the amount/type of contact work, even with matching initial scenes and inputs. Dynamic contact and support populations are intentionally not forced equal. Objects/body counts and fixture parameters remain equal. The table records the relevant primary scope/population observations; it does not add nested timings or assign every change a single cause.

| Body count / workers / steps | Narrow phase sorted → unsorted ms | Solving sorted → unsorted ms | Mean contacts sorted → unsorted |
| --- | ---: | ---: | ---: |
| rigid_body_container-n100-w1-steps600 | 0.009460 → 0.008929 | 0.021011 → 0.023723 | 39.19 → 41.26 |
| rigid_body_container-n100-w6-steps600 | 0.009484 → 0.008235 | 0.022259 → 0.023257 | 39.19 → 40.24 |
| rigid_body_container-n1000-w1-steps600 | 0.355074 → 0.278036 | 0.659314 → 0.721864 | 973.43 → 995.09 |
| rigid_body_container-n1000-w1-steps6000 | 0.481427 → 0.337804 | 0.951684 → 0.990878 | 1385.82 → 1404.81 |
| rigid_body_container-n1000-w6-steps600 | 0.254055 → 0.152474 | 0.728755 → 0.749012 | 973.43 → 1000.29 |
| rigid_body_container-n1000-w6-steps6000 | 0.335467 → 0.192444 | 0.995689 → 1.054735 | 1385.82 → 1400.64 |

For example, the 100-body/one-worker scene has about 5.3% more confirmed contacts and a higher solver-scope mean after removal, offsetting reduced narrow-phase cost. The short 1,000-body/one-worker primary saves roughly 77 µs in narrow phase but spends about 63 µs more solving; total mean is nearly flat. In the longer 1,000-body cases, the narrow-phase saving is roughly 143 µs and outweighs increased solving costs. Higher contact count alone does not quantitatively explain all solver-time changes; contact/manifold mix, numerical trajectory and host variability are not isolated.

The practical outcome is a useful measured improvement on the heavier/longer scenes, with an explicit small-scene downside and uncertainty. No universal latency/scaling guarantee, exact sort-only saving, new safety certification or resolution of the previous zero-contact broad-phase regression is claimed. The measured runtime is the post-removal version covered by [scoped correctness/work checks](solver-order-removal.md); the concurrent formatting-only source observation is recorded above.
