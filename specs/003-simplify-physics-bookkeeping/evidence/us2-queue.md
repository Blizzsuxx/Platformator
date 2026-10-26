# Queue membership reduction

The queue index is now the only mutable membership fact; its sentinel drives the read-only query. Grid enqueue/cancel and worker consumption no longer synchronize a separate boolean. First/middle/last cancellation still swap-repairs the moved index in O(1). Workers consume only their own index, retaining the fixed pointer batch through the join and serial reduction; Grid clears that batch without an added scan. No structural enqueue/cancel/erase may overlap the worker batch.

Build completed successfully and all five selected Debug bookkeeping/support/contact/cleanup/queued-deletion checks pass. See us2-queue-build.txt and us2-queue-tests.txt. Queue characterization exercises 131 candidates, three cancellations, 128 changed pairs, one/six workers, duplicate sync, consumed/requeue and empty batches. Existing multi-cell witness cases pass. No full-batch index/slot invariant is assumed while workers are consuming entries.
