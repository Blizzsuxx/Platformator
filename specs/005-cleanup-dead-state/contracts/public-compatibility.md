# Public Compatibility and Migration Contract

The public library is consumed via installed `include/platformator` headers. No scene or asset serialization migration is required. All behavior and public interfaces remain supported except the following scoped removals, subject to the fresh audit and any evidence-backed retention:

| Audit ID | Expected change | Consumer action |
| --- | --- | --- |
| C02 | Private `GameObject::hierarchyDepth` removal changes layout of the installed class. Public header-defined `getComponent<T>()` accesses later members. No public method is removed. | Rebuild library and every consuming translation unit together. Old binaries/objects cannot be assumed ABI-compatible. |
| C03 | Remove public `ClipPointWithData::accumulatedNormalImpulseBias`; contact layout changes. | Stop reading/writing the obsolete field and rebuild. Preserve use of real normal/tangent accumulated impulses and bias; do not substitute a split-impulse API. |
| C04 | Remove public `Audio::getGameManagerIndex()` and `setGameManagerIndex()` and the backing field; class layout may change. | Remove calls and rebuild. Audio is not indexed in a manager-owned collection; component ownership and transient playback semantics are unchanged. |
| C12 | Remove public `BoundingRadiusProjectionAxisBinding::bind()` and `BoundingRadiusProjectionProxy::setBoundingProjection()` with their dead internal caller. | Stop using the removed rebinding operations; rebuild. Existing binding construction and normal collider use remain. Do not promise a replacement rebinding interface. |

Implementation must update `docs/CPP_API_MIGRATION.md` (or the project’s current migration guide) with every actual declaration/layout impact and required actions, and correct current stale documentation about pair-slot consumers with a clear historical distinction. Preserve historical benchmark/evidence snapshots.

Build the source tree and installed/relocated independent consumer using retained APIs; compile the public headers individually. The package smoke test also exercises a Mario consumer. A passing current consumer cannot prove unknown external callers do not exist; the documented migration is mandatory. Existing tests that directly name the removed C03 field are migrated narrowly while keeping physical warm-start checks. No unrelated public declaration or ABI promise is changed intentionally.
