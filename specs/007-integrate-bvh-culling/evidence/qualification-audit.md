# Retained bundle qualification audit

The original convergence bundles and their manifests remain byte-for-byte
historical evidence. Their validation, timing, and resource identities were
reindexed with the expanded qualification key; every retained timing process
still maps to a validation identity, and every resource capture maps to timing
with the declared resource-hook difference. These pairings describe what the
older bundles recorded and do not qualify the new implementation source.

| Retained bundle | Validation | Timing | Resources | Historical pairing | Strict current qualification |
|---|---:|---:|---:|---|---|
| Replay | 248 | 1,240 | 248 | All 248 validations pair with five timing runs; all resources pair | Old trace headers and capture inputs predate required per-step `operation_counts` metadata |
| Actual engine | 176 | 880 | 176 | All 176 validations pair with five timing runs; all resources pair | Runtime input manifests do not record the actual scenario |
| Same-method controls | 40 | 200 | 40 | All 40 validations pair with five timing runs; all resources pair | Runtime input manifests do not record the actual scenario |
| Cold first-frame | 176 | 880 | — | All 176 validations pair with five timing runs | Runtime input manifests do not record the actual scenario |
| Profile calibration | 80 | 400 | — | All 80 validations pair with five timing runs | Replay inputs predate required per-step `operation_counts` metadata |

The old source fingerprint is
`f1c6298f8b6e7baaa3d4374236a9650e5345a2567f210dbfa3efcb3197b0aba1`.
No old capture is reused to qualify the corrected linear visibility path or
the stricter typed-engine checks. The updated coverage and paired-comparison
CSVs retain that source label, rename nominal changed counts as requested knobs,
and derive per-step operation counts from the immutable trace records. The raw
traces, hashes, manifests, and capture files are unchanged.

Fresh final-source validation, five-process timing, and matched resource
captures for the affected sparse/full linear visibility comparisons are
recorded separately in [the linear correction report](linear-correction.md).
