# Data Model: Focused Engine and Example Cleanup

## Request alias

- **Identity**: exact nonempty original request string within an asset type and runtime. A previously unseen spelling is a new request and undergoes full validation; a different kind cannot collide.
- **Value**: canonical resource identity or safe reference to an entry owned by the matching resource cache; never an owning asset reference that extends resource lifetime.
- **Creation**: only after validation, resolution, successful decode/deserialization and association with a live canonical resource; a separately validated alias can map to an already loaded resource.
- **Removal**: before associated resource is freed, remove every alias pointing to it. Clear all aliases during cache teardown. Failed resolution/loading and failed scene preparation must not leave a mapping to freed storage. Explicitly define rollback ownership in implementation.
- **Invariant**: every alias points to a currently live resource in the same kind/runtime. Alias storage is bounded by successfully resolved request strings for currently retained resources. Exact hit skips all filesystem checks; a miss performs first-time validation.

## Retained asset

- Canonical identity and type identify one runtime-owned texture, audio wrapper or animation clip. Equivalent validated aliases share this resource. Resource references retain existing acquisition/release behavior; final release removes the cached resource and all aliases. Explicitly loaded zero-reference cache entries follow existing ownership behavior, not a new retention policy.
- On nonempty miss: validate syntax and root containment, resolve the canonical file, check/load it, then add or associate a resource. A missing/unreadable/undecodable request is an error with original path; no default asset substitution. Empty no-asset values retain their existing entry-point semantics.
- Scene preparation may temporarily retain resources and must unwind partial acquisitions safely on failure. Animation clips may own nested texture references; destruction order and reentrant cache deletion must not invalidate active operations.

## Body / pair membership

- Each eligible body or pair belongs to zero or one position in each applicable vector; its corresponding index is `SIZE_MAX` when absent.
- Desired present + absent -> append and record index. Desired absent + present -> move last entry into removed position, repair moved index, pop, set removed index absent. Unchanged request -> no change. Last-element and alias-to-self cases must be safe.
- Each vector keeps its existing owner, eligibility predicate, update phase, and order semantics. Script-start queues use tombstones during callback traversal and are not this model.

## Debug object and Mario compatibility data

- Debug objects own names/vertices and can be moved during buffer growth without changing the rendered primitives. Debug-only storage remains unavailable in debug-off builds.
- Existing Mario camera scene records may include `cameraLead`; it remains accepted and serialized as before but has no camera behavior. Dead pointers/constants and undefined unused declarations have no live owner or consumer.
