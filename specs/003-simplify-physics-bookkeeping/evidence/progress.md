# Implementation status — 2026-10-26

43 of 46 tasks are complete. T046 removes canonical physics-solver ordering at the maintainer's explicit request, superseding the earlier deterministic-solving requirement. T038 performance acceptance and the same actual maintainer review under T043/T045 remain pending.

Solver sorting, comparator/helper/call, dirty flag and full index repair are gone; append/swap-remove membership and its local moved-index repair remain. Event dispatch ordering is unchanged. The support test checks accounting/events/finite state without worker-identical solved trajectories. Full normal/package checks: 24 pass/2 expected skips; work/support selection 2/2 and direct six-worker idle/churn checks pass. See [current scoped validation](solver-order-removal.md).

Current runtime fingerprint: cc6ce0456970f439e78ae6e664a7ab90066a12b8c2155c14a47483b68d6c1588. Earlier b87bc7a... sorted-runtime matrices, physical-equivalence captures and safety runs remain historical. No speedup or new full sanitizer certification is claimed. The original state reductions and support/lifetime invariants remain; no replacement ordering state was added. The prior broad-phase controls had no solver contacts, so their unexplained downside is not resolved by this deletion.

Readability findings remain pending. Requirement checklist markers and the existing staged index are preserved. This change is not committed or published.

Subsequent performance-only evaluation: [solver-order-performance.md](solver-order-performance.md) records 94 verified processes with immutable sorted/unsorted binaries and isolated identical dependencies. Three heavier/longer configurations improve about 6–8%; short one-worker gains vary and small one-worker means are higher. This does not establish an isolated sort-time saving, universal speedup or resolution of T038's earlier broad-phase downside. No engine algorithm or test/task status was changed for measurement; the concurrent formatting-only source observation is retained with the frozen-source provenance.
