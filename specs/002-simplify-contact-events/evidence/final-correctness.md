# Final correctness builds

cleanup-debug full build and complete platformator CTest: 25 registered, 23 passed, 2 expected skips because work/benchmark instrumentation is disabled in this build. Package smoke passes with rebuilt installed and relocated consumers/Mario. Production preset builds all 679 build steps, including vendor libraries, engine, runner, Mario, tests and independent public-header checks.

Final cleanup-benchmark runner/contract build and all three benchmark CTest checks pass, including ten Python reader/plot/fixture tests and the C++ frame/generation contract and the real-runner zero-warmup regression. See final-benchmark-tests.txt. Detailed logs are retained beside this file. Instrumented work results and sanitizers are separate evidence.
