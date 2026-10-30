# Render-culling coverage and evidence limits

## Reused historical artifacts

The pre-correction audit checked SHA-256 values for 140 retained trace files and matching image hashes across 35 cases, 21,000 corresponding frames, and four modes. It also checked 315 sampled submission-order/oracle records, six frozen feature-008 parity proofs, all 150 candidate source inputs, and the aggregate source fingerprint. Those stored-output checks found no mismatch. They remain evidence for source `d3418214c4f48416d943e95e44e8d8d3a7b296ae3ff23b3dd3d4c7d69948b93f`; they are not fresh post-correction proofs or timing evidence.

Two historical limitations are material. The clustered cases produced identical images despite a declared 10% motion fraction, so they are static image comparisons. The rotated trajectory callback was omitted from that earlier capture path, so those traces do not validate rotated movement. Both dispatch issues have focused current regressions, but the old artifacts have not been relabeled or regenerated.

## Current focused implementation checks

The current source adds focused regressions for published parent/world transforms observed before and after transform synchronization; textureless indexed fallback after restoring a texture and moving the camera; simultaneous crossing with batched visible-order maintenance; and actual callback dispatch for all dynamic fixture profiles, including clustered and rotated profiles. The capture verifier streams proof frames and retains only aggregate counts. Its 4,000-frame memory regression checks that parsed frame records are not retained.

The work-probe CTest run passed the geometry, cache/render, worker, trajectory, contract/package, index, and benchmark CLI cases (7/7). The Python capture suite passed 34/34. These focused checks do not replace the full lifecycle/numeric/mapping/failure matrix documented in the original validation contract.

## Focused pilot inventory

The durable pilot contains six cases: 10k mostly-offscreen static, 10k camera pan, 10k with a 1% moving subset, 10k fully moving crossing, 1k all-visible static, and a six-worker 10k crossing repeat. It has 54 independent timing processes (three strategies × six cases × three repetitions) and 24 resource proofs (the three strategies plus disabled mode for each case). Timing captures have 30 measured steps after five warmup steps and retain schema-v3 summaries, per-step samples, paired step-plus-render intervals, manifests, and logs.

The pilot is exploratory and intentionally has no per-frame identity/image traces or frozen feature-008/stock-engine controls. Resource proof mode checks the four modes against the same actual case parameters, renderer mapping, source, fixture, and harness. Timing mode checks the exact resource checksum and resolved trajectory, backend/mapping, build capabilities, raw sample count/order, and requested/effective strategy. It does not establish corresponding-frame rendering parity.

## Coverage not established by the pilot

- 50k populations, 10% motion, clustered/skewed layouts, 1k giant sprites, rotated projections, output resize, large coordinate offsets, scene-loaded populations, and headless/disabled timed controls.
- Fresh per-frame image, submission identity/order, and independent-oracle proofs for the selected timing cases.
- Fresh frozen feature-008 timing controls or stock-engine controls.
- Hardware renderer or GPU elapsed-time behavior; the pilot used SDL's software renderer with a 320×240 output and VSync off.
- Acceptance-grade repetition counts or a process-level confidence interval. Three processes with 30 measured steps are enough to screen for large changes, not to accept a small gain.
- Relocation of the full historical feature bundle. A relocation check was performed on the focused pilot bundle subset only.

The full acceptance matrix and SC-006 remain open. Keep the older campaign's unchecked tasks as historical obligations; do not treat a missing pilot workload as passing coverage.
