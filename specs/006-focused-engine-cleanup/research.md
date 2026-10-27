# Research: Focused Engine and Example Cleanup

## Required assets and request reuse

**Decision:** A nonempty request for texture, audio or animation, including animation-frame and behavior references, must either resolve and load the requested resource or throw an actionable error containing the original request. Preserve supported empty-path no-asset cases. Remove the optional fallback selection/warning path and stale required-fallback guards. Keep assets explicitly requested by their `missing.*` names as normal files. First check an exact raw request in a per-type runtime-owned alias lookup; on miss validate/canonicalize/resolve using current root policy, find or load by canonical identity, then publish the alias only after successful acquisition. Evict every alias for a resource before destruction and clear all on shutdown; failed decode/load publishes no alias. Preserve transactional scene preparation and rollback and nested animation frame ownership.

**Rationale:** `src/gamemanager.cpp` resolves before checking the canonical cache; `PathManager` invokes `weakly_canonical`, root containment and `is_regular_file` each time. Refcount releases call `freeTexture`, `freeAudio` and `freeAnimationClip`. Exact request keys avoid accidentally accepting a previously unseen invalid or changed symlink path. Canonical maps still deduplicate separately resolved aliases. Per-type ownership prevents cross-kind collisions.

**Alternatives:** Keep resolving on every hit (contradicts requested policy); globally cache paths forever (stale resources, unbounded lifetime); normalize paths before lookup (could skip first-time validation of a distinct spelling); invalidate on filesystem changes (explicitly excluded). Reverse alias-to-resource associations or per-resource alias lists both meet the contract; choose the simplest bounded deletion approach with measured eviction and retained-memory cost. Avoid O(all cached aliases) scans per eviction if they produce significant churn costs.

## Engine simplification

**Decision:** Remove the unused axis constructor/field/handler argument but retain `isPrimary` and the broad-phase algorithm. Remove unreachable fallback checks; remove ignored `Collision` constructor parameter, updating any live callers; remove the uncalled free behavior lookup. Prefer `getCurrentClip()` as canonical because existing implementation and serializer already use it; retain `getCurrentAnimationClip()` as a documented forwarding compatibility alias.

**Rationale:** The axis receiver ignores the value; checkpoints depend on the separate primary flag. Actual penetration correction uses contact separation; the public constructor argument is unused. Existing member behavior lookup is supported. Both animation getters currently return the same pointer.

**Alternatives:** Keep all duplication or silently remove the public animation alias; both lose clarity or needlessly break callers. The collision signature change is intentional and must be documented, including rebuild/relink considerations.

## Shared membership primitive

**Decision:** Factor only the body and pair `membership` implementations into a private typed helper; callers retain their eligibility predicates, list ownership and mutation boundaries. Use existing member access rules and mutable pair indexes where valid; do not spread `const_cast` or change externally observable ordering.

**Rationale:** Both use the same no-op/add/swap-remove/moved-index-repair algorithm. Pair and body state remain distinct. `cancelScriptStart` is an actively traversed callback queue and continues to leave holes.

**Alternatives:** Refactor every indexed list (out of scope and different semantics); retain exact duplicated code (contrary to explicit user request); generic container with policy machinery (unnecessary).

## Debug and Mario

**Decision:** Remove the empty user-declared DebugObject destructor if member ownership permits implicit moves; verify move/copy behavior and rendering. Retain copy operations if existing consumers need them. Remove unobserved Mario `targetBody`, enemy `collider`, three unused constants, and the two undefined player declarations after a fresh usage search. Preserve serialized `cameraLead` as an inert compatibility field, including existing scene data.

**Rationale:** The destructor suppresses implicit moves and vector growth may copy owned strings/vertices. The Mario field occurs in `examples/mario/level1.scene`; no implementation of camera lead is requested.

**Alternatives:** Delete the scene field or implement camera lead (unrequested behavior/serialization change); optimize unrelated drawing paths (outside scope).

## Validation approach

**Decision:** Freeze actual tracked and nonignored untracked inputs plus user diff and test-only shared fixtures; run baseline/candidate common behavior and separately test intentionally changed fallback behavior. Use focused independent work probes for filesystem calls and debug copies and matched process-level timing, plus normal engine benchmark workloads. Check debug on/off, public headers, installed consumers and Mario; record graphical/sanitizer limitations honestly.

**Rationale:** The baseline intentionally implements fallback, so raw expected outcomes cannot all match candidate; uncontrolled solver trajectories and headless graphical output also do not justify exact equivalence claims. Static slot-removal style timing is not a substitute for shared-load or debug-growth workloads.

**Alternatives:** Treat old tests as the desired contract or count timing probes as Release timings; both conceal intentional changes or distort results.
