# Cleanup inventory

| Mechanism | Disposition | Reason |
| --- | --- | --- |
| SceneRequest sequence/counter | Remove | No readers; replacing pending payload already supplies last-wins. |
| Private name-removal/deletion wrappers | Remove | Repository caller search finds none. |
| Duplicated rendered frame body | Consolidate | Maintain event/quit/pause/render behavior once. |
| Runtime and GameManager static pointers | Consolidate | Runtime owns the lifetime; preserve explicit shutdown before reset. |
| Mutable internal object collection accessor | Const | Public consumer already returns const; preserve ownership. |
| Duplicate sleep timer resets/unused impact argument | Simplify | Keep no-op guard, entry-only motion clear and predicate thresholds. |
| PhaseGuard and phase restrictions | Retain | Exception-safe nested restoration and safe mutation boundaries. |
| Reconciliation/drain queues and pending starts | Retain | Reentrant coalescing and exactly-once eligible starts. |
| Registry and file-ID lookup | Retain | Different identity contracts; retirement invalidates before reclamation. |
| Retirement batches | Retain | Support/events/callbacks finish before freeing old recipients. |
| Dirty transform queue/order/stamps | Retain | Incremental parent-first publication without duplicate subtree visits. |
| Body participation lists/indexes/queue | Retain | Changed-state work and constant-time removal rather than global scans. |
| Island scratch/rank/compression/two passes | Retain | Connected quiet/minimum-time decision with reusable storage. |
| Inline reset and newly-admitted candidate reset | Retain | Late impact waking admits candidates after force integration. |
| Net support contribution/delta queue | Retain | Replacement recipient and no interim-zero waking on transfer. |
| hierarchyDepth and dropped-clock statistics | Retain this pass | Avoid unrelated public layout/clock-accounting changes. |
| Public support/timer methods | Retain | Source compatibility; sparse local callers do not authorize removal. |

Additional selected simplification: GameManager and Runtime no longer explicitly default-initialize containers/owners already default-constructed in the same declared order. No resource order changed.

Final source reductions verified: one rendered implementation instead of two; two dead private methods removed; four duplicate-instance functions/storage and two publication sites removed; two unread sequence fields removed; const collection access; one transition-only timer reset and one actual impact-predicate input. Bracing expands physical line count but does not add runtime mechanisms.

Constitution recheck: correct alternative keeps incremental queues, indexed removal, reusable storage and local physical adjacency; no new known inefficient algorithm is retained by this change. Existing hierarchy rebuild cost is O(N) on hierarchy changes, dirty publication O(D log D + affected descendants) when ordering is needed. Force/movement remain independent oneTBB work with joined main-thread publication. Shared-root aggregation, solver and callbacks remain sequential because they share state/order. No foundational broad-phase changes or new Unity claims require additional architecture exceptions.

No replacement coordinator, new runtime flags, scheduler, global rescans or canonical solver ordering is proposed. Actual reductions and costs will be recorded after validation.
