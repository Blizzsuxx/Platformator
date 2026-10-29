# Requirement-to-Evidence Map

Current build/correctness evidence is under `benchmark/artifacts/linear-render-culling/20261029-t044-run-01/`; the root is Git-ignored and independently indexed. See [capture-index.md](capture-index.md), [final-validation.md](final-validation.md), and [RESULTS.md](RESULTS.md).

| Requirement / outcome | Evidence | Coverage status |
|---|---|---|
| FR-001–FR-003: ordered conservative bounds and strict-disjoint rejection | [static-rendering.md](static-rendering.md), `render_culling_geometry_test.cpp`, [rendering contract](../contracts/rendering.md) | Independent geometry oracle and software-renderer scenarios passed. |
| FR-004: same-frame movement, camera motion, lifecycle, and generations | [dynamic-rendering.md](dynamic-rendering.md), `render_culling_test.cpp`, [matrix-validation.md](matrix-validation.md) | Candidate CTest passed; bounded crossing, camera-pan, churn, and six-worker proofs match images and submission oracle. |
| FR-005–FR-007: mapping fallback, direct Camera compatibility, opt-out, and defaults | [dynamic-rendering.md](dynamic-rendering.md), [static-rendering.md](static-rendering.md), `benchmark_render_culling_cli_test.cpp` | Runtime/CLI checks and candidate package consumer pass. |
| FR-008–FR-010: headless isolation, warmed allocation, and retained state | [dynamic-rendering.md](dynamic-rendering.md), [resources.md](resources.md) | All 51 resource proofs pass; stable cases show no C++ `new`/`new[]` calls; headless render-work counters are zero. Probe coverage limits and process-wide RSS limits are explicit. |
| FR-011: rendered output, order, and independent visibility oracle | [static-rendering.md](static-rendering.md), [dynamic-rendering.md](dynamic-rendering.md), [matrix-validation.md](matrix-validation.md) | All 51 proof sets passed exact image hashes, ordered identity, and independent visibility checks; headless proofs correctly omit images. |
| FR-012: pinned baseline and comparable harness | [baseline-provenance.md](baseline-provenance.md), [capture-index.md](capture-index.md) | Pinned source archive, nine-file common-only patch, per-input hashes, separate binaries, proof/timing checksums, commands, and logs are persisted and verified. |
| SC-001–SC-003: matching pixels/order, conservative visibility, and no stale frames | [static-rendering.md](static-rendering.md), [dynamic-rendering.md](dynamic-rendering.md), [matrix-validation.md](matrix-validation.md) | Geometry, lifecycle, and all bounded image/order proofs pass. |
| SC-004: no culling-owned warmed allocation and headless work | [resources.md](resources.md), runtime work-probe tests | No mode-specific C++ allocation difference was observed. Aligned/C/SDL/driver allocations remain outside interception. |
| SC-005: representative complete-render benefit and regression assessment | [RESULTS.md](RESULTS.md), [performance-investigation.md](performance-investigation.md) | Sparse reductions of 73.9–85.0% exceed independent-run variation. All-visible mean estimates are positive but their 95% intervals include zero; no claim of zero overhead or a confirmed regression is made. Stock/headless control limits are reported. |
| Public docs, consumer compatibility, and final review | [final-validation.md](final-validation.md), [final-review.md](final-review.md) | Candidate CTest and final evidence audit pass; signoff is bounded to this host/backend/workload matrix. |

## Bounded cases

The 17-case matrix covers visible, sparse, offscreen, clustered, crossing, camera-pan, churn, rotated, and headless-crossing profiles at 1 and 6 workers where specified by the contract. The source and proof identities are in the bundle's independent audit JSON. The stock runner adds 60 controls for `broad_phase`, `moving_add_and_remove`, `persistent_contacts`, and `broad_phase_stress`.
