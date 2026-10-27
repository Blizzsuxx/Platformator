# Baseline invariants

Capture: `/tmp/platformator-dead-state-0j46vn51/baseline-invariants`, 18 jobs across one/six workers (600 measured steps, 120 warmup). Raw logs, schedules, manifests and snapshots are retained there. Every runner reported valid per-step invariants. Controlled pair-churn populations 10 and 1000 have identical per-step candidate/contact/Enter/Exit counts across workers.

Existing `benchmark/capture_cleanup.py::compare` matched 14 controlled/spatial groups. Two unconstrained rigid-body groups differed:

- rigid_body_container-w1/population-000100/run-00.manifest.json: Serial/parallel invariant differs: support_total
- rigid_body_container-w1/population-001000/run-00.manifest.json: Serial/parallel invariant differs: support_total

For 100 bodies, support totals were 74 (one worker) and 64 (six), and contacts 117/109; for 1000, supports were 599/558 and contacts 1361/1282. This is compatible with the pre-existing explicit maintainer waiver of unconstrained deterministic solver ordering in `specs/003-simplify-physics-bookkeeping/contracts/physics-lifecycle.md`; both runs independently passed physical invariants. No new exact trajectory equivalence is inferred. The feature contract is clarified to require equivalence for controlled cases and valid behavior for unconstrained solved scenes.
