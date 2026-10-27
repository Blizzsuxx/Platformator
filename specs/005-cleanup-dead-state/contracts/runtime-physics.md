# Runtime and Physics Preservation Contract

Applies to the candidate relative to a frozen, equivalent pre-cleanup baseline; use existing tests and contract tolerances rather than inventing new numerical thresholds.

## Broad phase and parallel work

1. Canonical identity, collider filtering, endpoint ties/order, segmented chunk bindings/indexes, checkpoints including fast-motion cancellation, split/merge, placement and migration retain their current behavior.
2. Cell-local candidate deltas and net witness counts preserve first-witness insertion, intermediate-witness survival and final-witness removal. Large colliders spanning multiple cells neither lose contacts prematurely nor manufacture duplicate transitions. Broad candidates, confirmed contacts and gameplay events remain distinct.
3. The eligible-pair queue feeds workers distinct pair-owned records. Work may remain parallel as today for sufficiently large batches and sequential for small ones; all workers finish before serial support/contact reduction, structural erasure, callbacks or reclamation. No new slots, worker pools or global scans are added.
4. Preserve queue cancellation, pair adjacency indexes, contact and solver membership indexes and pointer lifetime until those memberships and queued events have been drained. Removing the unused pair slot table must not change solver/event order or make memory reuse observable.
5. The scoped cleanup does not change local/global sort choice, adaptive scheduling, broad-phase geometry or tolerances. Check ties, long intervals, fast movement, chunk split/merge, insert/remove, cell migration and multicell duplicates through existing independent coverage plus focused behavior tests when missing.

## Contact response and sleeping

1. Preserve feature matching and normal/tangent impulse remapping/warm start. Remove assertions exclusively testing the obsolete bias accumulator without replacing the meaningful physical assertions.
2. Preserve Baumgarte penetration and restitution bias, one-contact scalar and two-contact block response, friction, trigger handling, support contribution transfer and wake/sleep behavior. No split-impulse feature or solver retuning is included.
3. Compare equivalent single- and multi-worker event traces and physical snapshots within established tolerances for controlled cases. For unconstrained solved scenes, preserve the existing waiver of cross-worker trajectory equality; verify finite physical state, support validity and event ordering for equivalent transition sets rather than demanding identical contacts or support totals. Exercise real dynamic contacts as well as triggers; no timing from invariant/probe runs is treated as release timing.

## GameObject and lifecycle

1. Parent/child creation, reparenting, immediate position reads, parent-before-child synchronization, overlapping dirty roots, transform cancellation and collider synchronization remain unchanged.
2. Component attach/replace/remove, activation, queued reconciliation, callbacks, object destruction and scene transitions preserve ordering, eligibility and allocation lifetimes. Existing single-main-thread structural mutation rule remains.
3. Fixed-step catch-up and dropped-step accounting in SimulationClock remain; only the never-read GameManager aggregate is eligible for removal.
4. Audio registration/autoplay/stop and the component's resource retention/destruction remain. Fire-and-forget tracks retain their own resource references until completed/cleared, including after the initiating component is gone.

## Asset paths and debug overlays

1. Preserve raw-request wording in diagnostics, canonical spelling and case policy, configured-root and symlink containment, required errors, optional fallback selection and scene/asset serialization.
2. Preserve supported buffered collider, contact and grid overlays, debug controls and disabled-tool operation. Existing code has no known pixel-level overlay oracle: collect a comparable graphical/captured smoke if supported; otherwise record the limitation and do not claim visual verification.
