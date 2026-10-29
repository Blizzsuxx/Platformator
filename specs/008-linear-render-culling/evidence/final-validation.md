# T044 Final Validation Record

## Builds and correctness gates

- Baseline and candidate each have a separate Release validation build with work probes on and Release timing build with work probes off; all four completed successfully. Compiler, dependency versions, build options, source inputs, and executable hashes are in [baseline-provenance.md](baseline-provenance.md).
- Baseline CTest: 29/29 passed. The package-smoke case was excluded for the pre-feature baseline because the candidate CTest performs the install/relocation check for the changed public package. Candidate CTest: 34/34 passed, including geometry/render culling, benchmark contracts, work-probe allocation checks, and package smoke. Logs are retained under `logs/correctness/` in the ignored bundle.
- The CMake source hash manifests were rehashed independently: all 131 baseline and 132 candidate inputs match their recorded paths and hashes. Both validation and timing probes report the respective aggregate source identity and the same fixture/harness hashes. The baseline common-support diff was independently compared with the pinned Git archive and limited to the nine declared common-support files.
- `nm -C --defined-only` found no validation allocation observer or global allocation replacement symbols in timing probes. Validation/resource captures are separate processes from the timing binaries.

## Captured evidence and independent verification

The persistent bundle contains 51 rendering proof sets and 51 matching resource proofs for 17 cases × pre-feature/disabled/linear, with image/order traces, normalized image hashes, independent oracle memberships, representative PPMs for 45 rendered proofs, resource counters, and per-file checksums. Headless cases contain no image and report zero rendering work.

All builds and correctness/resource captures finished before timing. The timing driver then ran 255 focused Release processes and 60 stock-engine controls, five independent fresh processes per comparison, using seed 0, 120 warmup steps, 600 measured steps, and `dt=0.008333333333333333`. Process order was shuffled. Each raw CSV has its sample CSV, manifest, and command/output log.

The strict probe validators accepted all proof triples. The separate `scripts/audit_timing.py` then independently rechecked proof checksums and membership/image/order constraints, all 315 timing/control command logs, raw sample and manifest hashes, per-row sample counts/order/numeric validity, summary scope means and counters, build/source identities, requested mode and fixture parameters, proof references, output completeness, and available worker/affinity metadata. It passed with 17 cases, 51 render proofs, 51 resource proofs, 255 focused runs, and 60 controls. Its checksum-backed result is `provenance/timing-audit.json` (SHA-256 `e289349f64db1efc253b8147bb6dba0278abb08122c5270f9d4ff70af83ea31b`).

The capture report SHA-256 is `ed7e25acbcaf21f88ef569ec70d4a8d5685d88988a470cd6ca47e0c76bba0a10`; `raw-index.csv` SHA-256 is `7ffc42a7c93ed1057536b4d55e1866ad7e4c33e195010b417b0c6b3f3bb7a3e3`. The report records the actual SDL renderer configuration: software, 320×240, scale 1×1, default viewport, no render target/logical presentation, VSync off. Headless probes report no renderer. Stock-runner manifests omit renderer identity, so those controls remain unverified for renderer identity.

## Performance interpretation

Sparse linear rendering lowered mean complete-render time by 73.9%, 80.0%, and 85.0% for 100, 1,000, and 10,000 sprites at one worker; the 10,000-sprite six-worker result was 84.7%. Welch intervals over the five independent process means exclude zero for those sparse comparisons. The 1,000-visible-sprite mean was positive by 0.0182 ms (1.85%) vs pre-feature and 0.0266 ms (2.73%) vs candidate-disabled at one worker; both intervals include zero. At six workers the corresponding estimates were +0.00505 ms (0.51%) and +0.00435 ms (0.44%), also with intervals including zero. Those positive estimates are reported as potential all-visible overhead; they do not establish either a repeatable regression or absence of one. Min/max overlap is not used to make that inference.

Focused headless crossing cases have zero render samples and zero render-work counters. Headless focused and stock-control complete-step intervals include zero. The rendered stock controls show small positive mean render deltas for `broad_phase` and `moving_add_and_remove`, with Welch intervals including zero; their stock manifests do not report actual renderer identity. Full spread, tails, and control results are in [RESULTS.md](RESULTS.md) and `provenance/performance-analysis.json`.

SC-005's mostly-offscreen benefit is demonstrated on this bounded SDL software-renderer matrix, with the all-visible estimates and their uncertainty retained as a tradeoff. This does not establish hardware-renderer behavior, GPU elapsed time, other host/platform behavior, or unmeasured populations.
