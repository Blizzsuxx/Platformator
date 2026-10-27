# Coverage and fixture assessment

Existing asset-root tests characterize fallback, input validation, canonical aliasing, root containment and required scene failures; candidate behavior needs missing and decode failure expectations, retained-file mutation, alias eviction and rollback checks. Physics bookkeeping, contact, pair-cleanup and geometry tests cover participation, contacts and witness survival; recheck gaps before adding cases. Independent asset and debug timing/copy fixtures are not yet available. No performance or no-resolution-call claim is established until an appropriate common fixture is captured.

Added common `benchmark/focused_cleanup_probe.cpp` for retained texture hits and debug-object growth. Its `asset-churn` mode releases the reference after each load; actual final-reference eviction still requires validation. It does not instrument filesystem calls, allocation, or copies/moves, and does not support rendered output. Those gaps remain unverified and require separate controls; this fixture does not justify treating those measurements as complete.

The common probe built successfully in the frozen benchmark build and its retained-hit, equivalent alias and release/reload modes returned success. It still cannot establish filesystem call counts, allocations or debug copy counts. `strace` and `valgrind` are unavailable on this host; no such counts are claimed. Further measurement fixtures or instrumentation remain needed for those specific checks.

A non-escalated `perf trace -e newfstatat` attempt also failed because BPF/tracing access was denied by the host (`/tmp/platformator-006-baseline/perf-trace.out`). The behavioral deletion test proves retained hits still return the asset, but it does not independently count all filesystem calls.
