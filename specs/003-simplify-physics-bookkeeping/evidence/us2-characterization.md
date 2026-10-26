# US2 characterization

Before runtime changes, both current and isolated baseline pass 5/5 selected checks: bookkeeping, supports, contacts, pair cleanup and queued deletion. Commands and original output: us2-current-characterization.txt and us2-baseline-characterization.txt. New tests use existing internal/public operations and independent expected queue membership, physical totals and histories.

Queue fixture begins with 131 independent queued real pair candidates, exercises repeated sync/deduplication and first/middle/last cancellation with moved-index checks, then evaluates 128 pairs under worker limits one/six, verifies consumed membership and requeues surviving contacts without synthetic Enter. Existing multi-witness/retirement tests remain.

Support fixture transfers one real support from response-enabled floor A to B within one boundary and checks that net zero does not wake the body; actual final support loss wakes. A replacement fixture observes old total zero and new total one from a real destruction callback before any retirement allocation is freed. Existing 128-support, reference swap, filter, trigger and script/sleep cases remain. Test-only additions are copied into the isolated tree; CMake and benchmark runtime fingerprint are unchanged since the final scaffold freeze.
