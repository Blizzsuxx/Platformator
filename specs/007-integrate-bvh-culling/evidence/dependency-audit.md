# Private Box2D dependency audit

## Source and license

`sha256sum --check UPSTREAM.sha256` passed for all 20 retained upstream
source, header, test, and license files in `vendored/box2d-3.2.0`. The pinned
MIT license and checksum manifest are installed with Platformator. The local
Box2D test executable `platformator_box2d_tree_test` passed in CTest.

The only Platformator adaptation is the private
[`box2d_tree_bridge.c`](../../../src/box2d_tree_bridge.c) bridge. It marks
create/move operations so Box2D can rebuild changed paths under
`NativePartial`, and exposes the pinned internal rebuild entry point for the
full-rebuild control. [`dynamicaabbtree.cpp`](../../../src/dynamicaabbtree.cpp)
is the C++ adapter. The upstream subset itself was not edited or expanded.

## SDK boundary

`platformator_box2d_tree` is a private object target folded into the static
engine archive. The installed public-header and exported-target scan in
[`package_smoke.cmake`](../../../tests/package_smoke.cmake) found no Box2D
headers, public symbols in declarations, CMake package, or client dependency.
The package smoke completed installed and relocated consumer checks, source
consumer checks, and the installed Mario example smoke run.

## Runner link audit

`nm -C` confirms `platformator_benchmark_runner` has only the undefined standard
`operator new` reference and contains no allocation-interceptor definition. The
separate `platformator_spatial_resource_runner` defines the allocation hooks,
including `operator new` and `operator delete`; the resource-hook object is not
linked into the timing executable. The resource runner contract test confirms
that resource captures declare themselves timing-ineligible and that the
normal timing executable rejects resource mode.

The vendored checksum audit was repeated after the final source build; all 20
retained upstream files still match `UPSTREAM.sha256` in
[`vendor-checksums-final.log`](final-validation/vendor-checksums-final.log).
The final `nm -C` timing/resource link audit is in
[`final-evidence-integrity.log`](final-validation/final-evidence-integrity.log).
The CTest Box2D tree test passed, and the final package smoke passed installed,
relocated, and source-consumer checks in
[`ctest-final.log`](final-validation/ctest-final.log).
