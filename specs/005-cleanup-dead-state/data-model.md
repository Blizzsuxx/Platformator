# Data Model: Clean Up Dead State and Internal Code

This describes existing ownership and state relationships to preserve while removing the audited redundancies. It does not prescribe new runtime entities.

## Audit disposition

Each C01–C14 record contains the finding ID, declaration/definition and call-site inventory across production, tests, examples, benchmarks and installed headers; disposition (`removed` or `retained`); evidence of no consumer or the precise active consumer/invariant; affected ABI/API; and validation references. Recheck at implementation start. An inaccessible external consumer is not evidence that a public API is unused, so C02/C03/C04/C12 require the compatibility policy in [public-compatibility.md](contracts/public-compatibility.md).

## Collider pair and witness state

- **Owner/identity**: Grid's `candidateCollisions` owns canonical collider-pair nodes keyed by runtime identities. Adjacency and queued/active lists hold borrowed pointers; no reference may survive removal.
- **Witness transition**: independent cell deltas are consolidated by pair. First positive witness publishes adjacency and queues narrow phase. Intermediate changes retain the same pair; final witness removal dequeues it, removes both adjacency links, removes physics participation/support and erases the owner. Duplicate cells must not duplicate transitions. Net delta and existing ordering must remain unchanged.
- **Active state**: `witnessCount`, both adjacency indexes, `narrowPhaseQueueIndex`, `contactIndex`, `solverIndex`, pair collision/version/result state and support contributors each have live consumers. Their sentinel/index invariants remain.
- **Removed candidate**: `pairSlot` plus `pairSlots` and `freePairSlots` form a write/recycle-only duplicate registry (C01). They are not object handles and have no generation or external resolution semantics. No result store or new index replaces them.
- **Worker boundary**: cell operations and eligible distinct narrow-phase pairs run in existing oneTBB batches. Each pair worker writes its own pair-owned data. After the batch joins, serial code processes transitions, support and memberships; structural erasure, event callbacks and reclamation cannot overlap borrowed pointers.

## Contact geometry and solver state

A `Collision` has reference/incident colliders, a normal, a feature-keyed manifold, active support flags and solver scratch. Each contact point stores physical separation, normal/tangent accumulated impulses and effective masses/bias. Feature remapping preserves the normal and tangent history for the matching contact; a new contact starts without previous impulses.

C03's separate accumulated bias impulse is stored/copied without affecting the current solver and is the only accumulator candidate for removal. The actual bias derives from penetration and restitution and feeds both scalar and block normal solving. Support flags and contribution credits govern body support transitions; C09 removes only uncalled private accessors. No new split-impulse or position solver is introduced.

## Object hierarchy and lifecycle

A GameObject has local position, parent/children, cached world position, dirty-queue index and `hierarchyOrder`. Hierarchy rebuild assigns parent-before-child order; dirty-root synchronization must publish each affected subtree under existing stamp and queue rules. C02's `hierarchyDepth` is written but not consulted. Removing it changes the installed GameObject layout even though it is private; inline public component lookup depends on offsets and consumers must rebuild.

Objects/components progress through staged/live, pending retirement, callbacks, unregistration and freeing under existing ownership/phase rules. C06/C07/C08 remove an uncalled constructor/wrapper/notification chain; live creation, scene publication, reconciliation, retirement and callback paths stay. C05 deletes only the manager's unobserved accumulated count, not SimulationClock's drop tracking or catch-up cap.

## Audio and asset ownership

An Audio component owns an optional mixer track and retains its bound AudioWrapper. Registration may autoplay; deactivation/removal stops playback; destruction releases the wrapper and track. Fire-and-forget playback is retained by the window's independent transient records until completion. C04's Audio manager index has no engine collection and can be removed along with public accessors, subject to source/ABI migration.

Path resolution takes a raw request, validates and canonicalizes it, checks root containment, and returns canonical/absolute paths plus fallback status. C13 removes the unused stored requested-path copy; local raw input still supplies validation errors and fallback diagnostics. Required errors, optional fallbacks and serialized asset spellings do not change.

## Debug output and public consumers

Active overlay generation feeds buffered debug objects to rendering and responds to existing controls; C14's direct drawing methods are disconnected and can be removed. No output record, coordinate algorithm or debug toggle is redesigned.

Installed consumers may depend on public contact fields, Audio accessors or projection-rebinding declarations. C03/C04/C12 remove those interfaces and may change layout; C02 changes GameObject layout. Rebuild the library and every consumer together. No serialized schema changes or new compatibility shims are implied.
