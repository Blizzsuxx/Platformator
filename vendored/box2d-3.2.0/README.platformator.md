# Box2D dynamic-tree subset

Source: https://github.com/erincatto/box2d, version 3.2.0.
Release reference: https://github.com/erincatto/box2d/releases/tag/v3.2.0.
Extracted from Platformator's original vendored import at
`0e51c1cffeed514e306409b8b4dfe69b8afb61f1`.

All files listed in `UPSTREAM.sha256` are retained verbatim from that import,
including the MIT license and upstream dynamic-tree tests. The version label
comes from the imported source; the checksum manifest pins the exact retained
bytes independently of that label. Run `sha256sum --check UPSTREAM.sha256` in
this directory to verify them. `CMakeLists.txt` and this README are maintained
by Platformator.

Only `dynamic_tree.c`, `aabb.c`, `core.c`, and `math_functions.c` are compiled.
The headers are their dependency closure; retaining the original geometry/type
declarations does not provide the rest of the Box2D library. Worlds, contacts,
solvers, joints, task scheduling, samples and the original build system are
excluded. No dependency downloads are performed.

The private object target `platformator_box2d_tree` is incorporated into
Platformator's static archive. Its headers and symbols are not a public SDK
API. Installed consumers need no separate Box2D library or CMake package.
The MIT license and this provenance file are installed with Platformator.

`src/dynamicaabbtree.h` in Platformator provides the C++ adapter, independent of
runtime objects and assets. Proxy user data is an opaque 64-bit value, suitable
for IDs chosen by future physics/rendering consumers. Proxy IDs belong to one
tree and become invalid after removal. The adapter is not thread-safe: callers
must synchronize mutation against queries. Query callbacks must not mutate the
tree or let exceptions escape through the C callback.
Bounds must be finite and ordered. Proxy widths/heights must be below upstream's
`B2_HUGE` limit (100,000 with the default length units); query regions are not
subject to that proxy-size restriction. The adapter rejects invalid bounds
before they reach Box2D assertions.

`src/box2d_tree_bridge.c` provides explicit rebuild access through Box2D's
internal header. This is deliberately tied to the pinned subset. Updates use
the public remove/reinsert operation; fat-bound policies and batch refitting
remain future prototype choices. Public move operations do not mark leaves
for Box2D's internal incremental rebuild bookkeeping, so the adapter exposes
full rebuilds rather than promising partial rebuild behavior.

Debug builds enable Box2D's internal validation; Release builds use upstream's
normal optimized behavior. C17, default single-precision bounds, the upstream
SIMD selection, and disabled floating-point contraction match the relevant
upstream build settings. Platformator sanitizer options also instrument these
objects. Linux/GCC is validated; other platforms remain unverified.

CTest runs the retained upstream dynamic-tree suite and adapter integration
tests. This addition prepares a dependency for future BVH experiments; the
current collider broad phase and sprite rendering remain unchanged.
