# Baseline correctness

The isolated current runtime builds cleanup-debug, cleanup-work and cleanup-benchmark successfully. Debug physics/lifecycle selection: 15 registrations, 14 passed and one expected work-disabled skip. Work/contact/support selection: 3/3 passed. Benchmark contracts: 3/3 passed, including zero-warmup runner and Python suite. Original commands/output are baseline-normal-tests.txt, baseline-work-tests.txt and baseline-benchmark-tests.txt. No baseline failure observed in these selections; full desktop leak/race checks are separate later work.
