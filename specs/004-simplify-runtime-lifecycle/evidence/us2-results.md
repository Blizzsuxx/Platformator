# US2 results

Physics/transform selection: 9 registrations, 8 passed and 1 work-build-only skip. This includes stable sleeping-pile regressions, extended bookkeeping/support cases and contact/geometry checks. Work tests pass at 1/2/6 workers including zero warmed C++ allocation, no idle body work, exactly-once subtree publication and fixed-peak contact/event storage reuse.

Logs: bin/runtime-cleanup-validation/logs/us2-tests.txt and us2-work-tests.txt. Baseline characterization passed with the same new assertions. Both sleep passes, inline/reset-on-admission initialization, path compression/rank, support batching, queued participation and solver order are retained. Final Release comparisons are pending the integrated capture stage; no speedup is inferred from formatting or fewer writes in the source.
