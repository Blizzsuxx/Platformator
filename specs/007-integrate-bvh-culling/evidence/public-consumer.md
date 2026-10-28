# SDK and public-consumer validation (T031)

`platformator_package_smoke` passed after rebuilding the package smoke fixture. The script built and
ran a source-embedded consumer, an installed consumer, and a consumer after relocating the installed
prefix. The consumer selects `BroadPhaseMode::DynamicBvh`, then exercises the established Collider
projection accessors (`getXProjections`, `getYProjections`), Grid diagnostics
(`getGridCellRange`, `getGridCellRangeCache`, `hasValidGridCellRange`, `getIsRegisteredInGrid`),
PhysicsManager registration and the new selected-backend diagnostic. Its expectations confirm that
BVH registration does not claim Grid membership and leaves the literal Grid range invalid.

The same smoke also built and ran the installed Mario example. The package script scanned installed
public headers and CMake target exports and found no Box2D headers, types, package or link target in
the client surface. Public-header compile coverage is registered by
`cmake/PlatformatorPublicHeaders.cmake`; the probes-enabled build's
`platformator_public_headers` target completed successfully.

SDK compatibility remains source compatibility, not a binary ABI promise. The runtime-selection
contract requires rebuilding the SDK and its consumers together when the SDK changes.
