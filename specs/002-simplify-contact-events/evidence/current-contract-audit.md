# Current contract audit

Searched current src/, include/platformator/, tests/, benchmark Python tools/tests and README, docs/, examples/mario/ and ui/src/platformator_ui/. No live Stay callback, event kind, observer vector/index/order flag, subscription refresh, old geometry/startup entry name, duplicate solver projection or unused narrow-phase aggregation remains.

Intentional negative-test references remain in tests/contact_events_test.cpp (compile-time rejection of the removed callback/event) and benchmark/tests/test_sweep_scaling.py (rejecting the removed metric and historical schema 2). These do not define supported runtime surfaces. Documentation mentions removal for migration. Historical specs, baseline patches/captures and benchmark baselines/results remain unchanged.

Removed the obsolete commented refreshColliderComponent API and unused private-header includes, replacing the broad concurrent-vector include with the actual enumerable_thread_specific dependency. The retained all-contact/solver/body/support collections have explicit physical/invariant consumers. Geometry relocation is token-verified in geometry-relocation.txt.
