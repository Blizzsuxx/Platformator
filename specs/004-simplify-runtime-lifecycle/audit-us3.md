# US3: runtime and scene lifetime audit

Before source: bin/runtime-cleanup-validation/baseline-source-v2/src/{runtime,gamemanager,runtimeaccess}.cpp. Final sources: [Runtime](../../src/runtime.cpp), [RuntimeAccess](../../src/runtimeaccess.cpp), [GameManager](../../src/gamemanager.cpp).

**J6 construction/access**: Runtime constructs paths and GameManager, then publishes itself. RuntimeAccess resolves that owner and checks manager availability at its boundary. GameManager's separate static pointer and publication/getter/fallback machinery are removed. No later synchronization of two global pointers is required; no-active-owner error behavior is preserved.

**J6 scene selection**: `requestScene` replaces an optional request payload. An absent pending request means no work; a selected request with no path clears; a path prepares a replacement. The unread sequence/counter are removed. `commitMutations` selects once, prepares before retiring the old scene, preserves it on preparation failure, then reconciles and destroys old allocations before publication. File IDs, registry identities and next-step startup retain their original contracts.

**J6 teardown**: Runtime explicitly calls shutdown while both owner and manager are accessible. Shutdown excludes forbidden mutation, reconciles work/support/events, delivers destruction, releases resources, and becomes complete. Only then does Runtime reset its manager and clear its sole active pointer. The manager destructor's shutdown call is idempotent. No callback is moved into the reset window. Fresh runtimes cannot revive old handles.

Evidence: [characterization](evidence/us3-characterization.md), [results](evidence/us3-results.md). Actual maintainer judgment of this route's auditability remains pending.
