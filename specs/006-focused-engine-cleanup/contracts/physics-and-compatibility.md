# Physics, Rendering and Compatibility Contract

## Physics and membership

- Axis plumbing removed by S01 carries no semantic data; preserve `isPrimary` checkpoint processing, extrema tie/order, endpoint/chunk binding repair, fast-motion cancellation, split/merge and per-cell placement/migration. Conservative overlap coverage and first/final net witness changes remain unchanged, including multicell pairs and large AABBs.
- Cell work and pair-local narrow-phase work complete before serial witness/contact/support reduction, callbacks, structural reclamation and pair freeing. Preserve existing oneTBB partitioning, solver order, event and callback eligibility rules.
- Shared membership code preserves exact add/no-op/swap-remove behavior and all per-list indexes for bodies and pairs; test middle and last removal, repeated requests, membership transitions, and retirement. Existing queue-drain boundaries and script-start tombstones are unchanged.
- Removing the unused public collision-constructor penetration argument does not change separation-based bias, warm start, normal/tangent impulses or contact solving. Document old and new signatures and rebuild needs; update in-repository consumers. Do not add a second penetration value.

## Other interfaces

- `getCurrentClip()` is preferred; `getCurrentAnimationClip()` remains an equivalent source/binary-compatible forwarding alias where feasible. Verify playing/stopped/scene-roundtrip results and document both names.
- Existing serialized Mario `cameraLead` stays accepted and round-trippable, with existing inert behavior. Remove only independently verified dead example fields, constants and declarations. Build and exercise Mario under the installed consumer smoke path.
- Debug-enabled vector growth may move objects; member ownership, scene rendering, draw order and controls remain unchanged. Debug-disabled builds continue to work. If screenshots cannot be obtained, record visual equivalence as unverified.
