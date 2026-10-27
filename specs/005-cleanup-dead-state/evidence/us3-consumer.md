# Public headers and consumer smoke

The debug-tools-on build compiled `platformator_public_headers`. The full debug CTest run passed `platformator_package_smoke`, which exercises source and relocated installed consumers and Mario, with output in `/tmp/platformator-dead-state-0j46vn51/candidate-debug-ctest.log`. Rebuilding these consumers verifies retained interfaces; it does not preserve the intentionally removed APIs or binary compatibility.
